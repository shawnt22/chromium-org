// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utilities to handle vendored third-party crates.

use crate::config::{BuildConfig, CrateConfig};
use crate::deps;
use crate::manifest;

use std::fmt::{self, Display};
use std::fs;
use std::hash::Hash;
use std::num::NonZero;
use std::path::{Path, PathBuf};
use std::str::FromStr;

use anyhow::{bail, Context, Result};
use semver::Version;
use serde::{Deserialize, Serialize};

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Visibility {
    /// The crate can be used by any build targets.
    Public,
    /// The crate can be used by only third-party crates.
    ThirdParty,
    /// The crate can be used by any test target, and in production by
    /// third-party crates.
    TestOnlyAndThirdParty,
}

/// Returns a default of `ThirdParty`, which is the most conservative option and
/// generally what we want if one isn't explicitly computed.
impl std::default::Default for Visibility {
    fn default() -> Self {
        Visibility::ThirdParty
    }
}

/// An `Epoch` represents a set of crate versions where no API breaking changes
/// are expected. For example, `png-0.17.15` doesn't make any API breaking
/// changes on top of `png-0.17.14` and therefore both of those versions are
/// considered to be in the same `Epoch`: `png-v0_17`. (Note that `png-0.17.15`
/// adds new APIs and therefore downgrading to 0.17.14 *would* be a breaking
/// change.)
///
/// An `Epoch` is used in paths like: `//third_party/rust/png/v0_18` and
/// `//third_party/rust/chromium_crates_io/vendor/png-v0_18'.
///
/// The implementation below tries to ensure that `Epoch` matches the following
/// wording from https://doc.rust-lang.org/cargo/reference/specifying-dependencies.html#default-requirements:
/// "Versions are considered compatible if their left-most non-zero
/// major/minor/patch component is the same.".
#[derive(Clone, Copy, Debug, Deserialize, Eq, Hash, Ord, PartialEq, PartialOrd, Serialize)]
#[serde(from = "EpochString", into = "EpochString")]
pub enum Epoch {
    /// Epoch when major version >= 1.
    Major(NonZero<u64>),
    /// Epoch when major version is 0. The field is the minor version.
    Minor(NonZero<u64>),
    /// Epoch when major and minor version are 0. The field is the patch
    /// version.
    Patch(u64),
}

impl Epoch {
    /// Get the semver version string for this Epoch. This will only have a
    /// non-zero major component, or a zero major component and a non-zero
    /// minor component, or a zero major+minor components and a non-zero
    /// patch component. Note this differs from Epoch's `fmt::Display` impl.
    pub fn to_version_string(&self) -> String {
        match *self {
            Epoch::Major(major) => format!("{major}"),
            Epoch::Minor(minor) => format!("0.{minor}"),
            Epoch::Patch(patch) => format!("0.0.{patch}"),
        }
    }

    /// A `semver::VersionReq` that matches any version of this epoch.
    pub fn to_version_req(&self) -> semver::VersionReq {
        let (major, minor, patch) = match self {
            Self::Major(x) => (x.get(), None, None),
            Self::Minor(x) => (0, Some(x.get()), None),
            Self::Patch(x) => (0, Some(0), Some(*x)),
        };
        semver::VersionReq {
            comparators: vec![semver::Comparator {
                // "^1" is the same as "1" in Cargo.toml.
                op: semver::Op::Caret,
                major,
                minor,
                patch,
                pre: semver::Prerelease::EMPTY,
            }],
        }
    }

    /// Compute the Epoch from a `semver::Version`. This is useful since we can
    /// parse versions from `cargo_metadata` and in Cargo.toml files using the
    /// `semver` library.
    pub fn from_version(version: &Version) -> Self {
        if let Ok(nonzero_major) = version.major.try_into() {
            return Self::Major(nonzero_major);
        }

        if let Ok(nonzero_minor) = version.minor.try_into() {
            Self::Minor(nonzero_minor)
        } else {
            Self::Patch(version.patch)
        }
    }

    /// Get the requested epoch from a supported dependency version string.
    /// `req` should be a version request as used in Cargo.toml's [dependencies]
    /// section.
    ///
    /// `req` must use the default strategy as defined in
    /// https://doc.rust-lang.org/cargo/reference/specifying-dependencies.html#specifying-dependencies-from-cratesio
    pub fn from_version_req_str(req: &str) -> Self {
        // For convenience, leverage semver::VersionReq for parsing even
        // though we don't need the full expressiveness.
        let req = semver::VersionReq::from_str(req).unwrap();
        // We require the spec to have exactly one comparator, which must use
        // the default strategy.
        assert_eq!(req.comparators.len(), 1);
        let comp: &semver::Comparator = &req.comparators[0];
        // Caret is semver's name for the default strategy.
        assert_eq!(comp.op, semver::Op::Caret);
        match (comp.major.try_into(), comp.minor) {
            (Ok(nonzero_major), _) => Epoch::Major(nonzero_major),
            (Err(_zero_major), Some(minor)) => match (minor.try_into(), comp.patch) {
                (Ok(nonzero_minor), _) => Epoch::Minor(nonzero_minor),
                (Err(_zero_minor), Some(patch)) => Epoch::Patch(patch),
                (Err(_zero_major), None) => panic!("invalid version req {req}"),
            },
            (Err(_zero_major), None) => panic!("invalid version req {req}"),
        }
    }
}

// This gives us a ToString implementation for free.
impl Display for Epoch {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            // These should never return Err since formatting an integer is
            // infallible.
            Epoch::Major(major) => f.write_fmt(format_args!("v{major}")).unwrap(),
            Epoch::Minor(minor) => f.write_fmt(format_args!("v0_{minor}")).unwrap(),
            Epoch::Patch(patch) => f.write_fmt(format_args!("v0_0_{patch}")).unwrap(),
        }

        Ok(())
    }
}

impl FromStr for Epoch {
    type Err = EpochParseError;

    /// A valid input string is of the form:
    /// * "v{i}", where i >= 1, or
    /// * "v0_{i}", where i >= 1
    /// * "v0_0_{i}", where i >= 1
    ///
    /// Any other string is invalid. If the "v" is missing, there are extra
    /// underscore-separated components, or there are two numbers but both
    /// are 0 or greater than zero are all invalid strings.
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        // Split off the "v" prefix.
        let Some(s) = s.strip_prefix('v') else {
            return Err(EpochParseError::BadFormat);
        };

        // Split and parse the major, minor, and patch version numbers.
        let parts = s
            .split('_')
            .map(|substr| substr.parse::<u64>().map_err(EpochParseError::InvalidInt))
            .collect::<Result<Vec<_>, _>>()?;

        // Get the final epoch.
        match parts.as_slice() {
            &[major] => {
                let Ok(nonzero_major) = major.try_into() else {
                    return Err(EpochParseError::BadVersion);
                };
                Ok(Epoch::Major(nonzero_major))
            }
            &[0, minor] => {
                let Ok(nonzero_minor) = minor.try_into() else {
                    return Err(EpochParseError::BadVersion);
                };
                Ok(Epoch::Minor(nonzero_minor))
            }
            &[0, 0, patch] => Ok(Epoch::Patch(patch)),
            &[_, _] | &[_, _, _] => Err(EpochParseError::BadVersion),
            _ => Err(EpochParseError::BadFormat),
        }
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum EpochParseError {
    /// An integer could not be parsed where expected.
    InvalidInt(std::num::ParseIntError),
    /// The string was not formatted correctly. It was missing the 'v' prefix,
    /// was missing the '_' separator, or had a tail after the last integer.
    BadFormat,
    /// The epoch had an invalid combination of versions: e.g. "v0_0", "v1_0",
    /// "v1_1".
    BadVersion,
}

impl Display for EpochParseError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        use EpochParseError::*;
        match self {
            InvalidInt(parse_int_error) => parse_int_error.fmt(f),
            BadFormat => f.write_str("epoch string had incorrect format"),
            BadVersion => f.write_str("epoch string had invalid version"),
        }
    }
}

impl std::error::Error for EpochParseError {}

/// A crate name normalized to the format we use in //third_party.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct NormalizedName(String);

impl NormalizedName {
    /// Wrap a normalized name, checking that it is valid.
    pub fn new(normalized_name: &str) -> Option<NormalizedName> {
        let converted_name = Self::from_crate_name(normalized_name);
        if converted_name.0 == normalized_name {
            Some(converted_name)
        } else {
            None
        }
    }

    /// Normalize a crate name. `crate_name` is the name Cargo uses to refer to
    /// the crate.
    pub fn from_crate_name(crate_name: &str) -> NormalizedName {
        NormalizedName(
            crate_name
                .chars()
                .map(|c| match c {
                    '-' | '.' => '_',
                    c => c,
                })
                .collect(),
        )
    }

    /// Get the wrapped string.
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for NormalizedName {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}

/// Identifies a crate available in some vendored source. Each crate is uniquely
/// identified by its Cargo.toml package name and version.
#[derive(Clone, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct VendoredCrate {
    pub name: String,
    pub version: Version,
}

impl fmt::Display for VendoredCrate {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} {}", self.name, self.version)
    }
}

impl VendoredCrate {
    pub fn normalized_name(&self) -> NormalizedName {
        NormalizedName::from_crate_name(&self.name)
    }
}

pub struct CrateFiles {
    /// The list of all source files that are part of the crate and may be used
    /// by rustc when building any part of the crate, as absolute paths. These
    /// files are those found under the crate root.
    pub sources: Vec<PathBuf>,
    /// The list of all input files that are part of the crate and may be used
    /// by rustc when building any part of the crate, as absolute paths. This
    /// may contain .rs files as well that are part of other crates and which
    /// may be include()'d or used through module paths.
    pub inputs: Vec<PathBuf>,
    /// The list of all native lib files that are part of the crate and may be
    /// depended on through `#[link]` directives. These files are those found
    /// under the crate root.
    pub native_libs: Vec<PathBuf>,
    /// Like `sources` but for the crate's build script.
    pub build_script_sources: Vec<PathBuf>,
    /// Like `inputs` but for the crate's build script.
    pub build_script_inputs: Vec<PathBuf>,
}

impl CrateFiles {
    fn new() -> Self {
        Self {
            sources: vec![],
            inputs: vec![],
            native_libs: vec![],
            build_script_sources: vec![],
            build_script_inputs: vec![],
        }
    }

    /// Sorts the CrateFiles for a deterministic output.
    fn sort_and_dedup(&mut self) {
        fn doit(vec: &mut Vec<PathBuf>) {
            vec.sort_unstable();
            vec.dedup();
        }
        doit(&mut self.sources);
        doit(&mut self.inputs);
        doit(&mut self.native_libs);
        doit(&mut self.build_script_sources);
        doit(&mut self.build_script_inputs);
    }
}

/// Get the subdir name containing `id` in a `cargo vendor` directory.
pub fn std_crate_path(id: &VendoredCrate) -> PathBuf {
    format!("{}-{}", id.name, id.version).into()
}

#[derive(Debug, PartialEq, Eq)]
pub enum IncludeCrateTargets {
    LibOnly,
    LibAndBin,
}

/// Collect the source and input files (i.e. `CrateFiles`) for each library that
/// is part of the build.
pub fn collect_crate_files(
    p: &deps::Package,
    config: &BuildConfig,
    include_targets: IncludeCrateTargets,
) -> Result<(VendoredCrate, CrateFiles)> {
    let mut files = CrateFiles::new();

    struct RootDir {
        path: PathBuf,
        collect: CollectCrateFiles,
    }

    let mut root_dirs = Vec::new();
    if let Some(lib_target) = p.lib_target.as_ref() {
        let lib_root = lib_target.root.parent().expect("lib target has no directory in its path");
        root_dirs.push(RootDir { path: lib_root.to_owned(), collect: CollectCrateFiles::Internal });

        let mut extend_root_dirs = |entry_getter: &dyn Fn(&CrateConfig) -> &Vec<PathBuf>,
                                    collect_kind| {
            root_dirs.extend(
                config
                    .get_combined_set(&p.package_name, entry_getter)
                    .into_iter()
                    .map(|path| RootDir { path: lib_root.join(path), collect: collect_kind }),
            );
        };
        extend_root_dirs(&|cfg| &cfg.extra_src_roots, CollectCrateFiles::ExternalSourcesAndInputs);
        extend_root_dirs(&|cfg| &cfg.extra_input_roots, CollectCrateFiles::ExternalInputsOnly);
        extend_root_dirs(
            &|cfg| &cfg.extra_build_script_src_roots,
            CollectCrateFiles::BuildScriptExternalSourcesAndInputs,
        );
        extend_root_dirs(
            &|cfg| &cfg.extra_build_script_input_roots,
            CollectCrateFiles::BuildScriptExternalInputsOnly,
        );
        extend_root_dirs(&|cfg| &cfg.native_libs_roots, CollectCrateFiles::LibsOnly);
    }
    if include_targets == IncludeCrateTargets::LibAndBin {
        for bin in &p.bin_targets {
            let bin_root = bin.root.parent().expect("bin target has no directory in its path");
            root_dirs
                .push(RootDir { path: bin_root.to_owned(), collect: CollectCrateFiles::Internal });
        }
    }

    for root_dir in root_dirs {
        recurse_crate_files(&root_dir.path, &mut |filepath| {
            collect_crate_file(&mut files, root_dir.collect, filepath)
        })
        .with_context(|| {
            format!(
                "Failed to process `{}` path.  This path came from {} for {p}",
                root_dir.path.display(),
                root_dir.collect.as_origin_msg(),
            )
        })?;
    }
    files.sort_and_dedup();

    let crate_id = VendoredCrate { name: p.package_name.clone(), version: p.version.clone() };
    Ok((crate_id, files))
}

/// Traverse vendored third-party crates in the Rust source package. Each
/// `VendoredCrate` is paired with the package metadata from its manifest. The
/// returned list is in unspecified order.
pub fn collect_std_vendored_crates(vendor_path: &Path) -> Result<Vec<VendoredCrate>> {
    let mut crates = Vec::new();

    for vendored_crate in fs::read_dir(vendor_path)? {
        let vendored_crate: fs::DirEntry = vendored_crate?;
        if !vendored_crate.file_type()?.is_dir() {
            continue;
        }

        let crate_id = get_vendored_crate_id(&vendored_crate.path())?;

        // Vendored crate directories can be named "{package_name}" or
        // "{package_name}-{version}", but for now we only use the latter for
        // std vendored deps. For simplicity, accept only that.
        let dir_name = vendored_crate.file_name().to_string_lossy().into_owned();
        let std_path = std_crate_path(&crate_id).to_str().unwrap().to_string();
        let std_path_no_version = std_path
            .rfind('-')
            .map(|pos| std_path[..pos].to_string())
            .unwrap_or(std_path.to_string());
        if std_path != dir_name && std_path_no_version != dir_name {
            bail!("directory name {dir_name} does not match package information for {crate_id:?}");
        }
        crates.push(crate_id);
    }

    Ok(crates)
}

#[derive(Copy, Clone, PartialEq, Eq)]
enum CollectCrateFiles {
    /// Collect .rs files and store them as `sources` and other files as
    /// `inputs`. These are part of the crate directly.
    Internal,
    /// Collect .rs files, .md files and other file types that may be
    /// include!()'d into the crate, and store them as `inputs`. These are not
    /// directly part of the crate.
    ExternalSourcesAndInputs,
    /// Like ExternalSourcesAndInputs but excludes .rs files.
    ExternalInputsOnly,
    /// Like `ExternalSourcesAndInputs` but for build scripts.
    BuildScriptExternalSourcesAndInputs,
    /// Like `ExternalInputsOnly` but for build scripts.
    BuildScriptExternalInputsOnly,
    /// Collect .lib files and store them as `native_libs`. These can be
    /// depended on by the crate through `#[link]` directives.
    LibsOnly,
}

impl CollectCrateFiles {
    fn as_origin_msg(&self) -> &'static str {
        use CollectCrateFiles::*;
        match self {
            Internal => "crate metadata and sources",
            ExternalSourcesAndInputs => "`extra_src_roots` entry in `gnrt_config.toml`",
            ExternalInputsOnly => "`extra_input_roots` entry in `gnrt_config.toml`",
            BuildScriptExternalSourcesAndInputs => {
                "`extra_build_script_src_roots` entry in `gnrt_config.toml`"
            }
            BuildScriptExternalInputsOnly => {
                "`extra_build_script_input_roots` entry in `gnrt_config.toml`"
            }
            LibsOnly => "`native_libs_roots` entry in `gnrt_config.toml`",
        }
    }
}

// Adds a `filepath` to `CrateFiles` depending on the type of file and the
// `mode` of collection.
fn collect_crate_file(files: &mut CrateFiles, mode: CollectCrateFiles, filepath: &Path) {
    use CollectCrateFiles::*;
    match filepath.extension().and_then(std::ffi::OsStr::to_str) {
        Some("rs") => match mode {
            Internal => files.sources.push(filepath.to_owned()),
            ExternalSourcesAndInputs => files.inputs.push(filepath.to_owned()),
            ExternalInputsOnly => (),
            BuildScriptExternalSourcesAndInputs => {
                files.build_script_inputs.push(filepath.to_owned())
            }
            BuildScriptExternalInputsOnly => (),
            LibsOnly => (),
        },
        // md: Markdown files are commonly include!()'d into source code as docs.
        // h: cxxbridge_cmd include!()'s its .h file into it.
        // json: json files are include!()'d into source code in the wycheproof crate
        // data: .rs.data files used by ICU4X
        // dat: zoneinfo.dat file from jiff-tzdb
        Some("md") | Some("h") | Some("json") | Some("data") | Some("dat") => match mode {
            Internal | ExternalSourcesAndInputs | ExternalInputsOnly => {
                files.inputs.push(filepath.to_owned())
            }
            BuildScriptExternalSourcesAndInputs | BuildScriptExternalInputsOnly => {
                files.build_script_inputs.push(filepath.to_owned())
            }
            LibsOnly => (),
        },
        Some("lib") if mode == LibsOnly => files.native_libs.push(filepath.to_owned()),
        _ => (),
    };
}

/// Recursively visits all files under `path` and calls `f` on each one.
///
/// The `path` may be a single file or a directory.
pub fn recurse_crate_files(path: &Path, f: &mut dyn FnMut(&Path)) -> Result<()> {
    fn recurse(path: &Path, root: &Path, f: &mut dyn FnMut(&Path)) -> Result<()> {
        let meta = std::fs::metadata(path)
            .with_context(|| format!("Couldn't read metadata of `{}`", path.display()))?;
        if !meta.is_dir() {
            // Working locally can produce files in tree that should not be considered, and
            // which are not part of the git repository.
            //
            // * `.devcontainer/` may contain .md files such as a README.md that are never
            //   part of the build.
            // * `.vscode/` may contain .md files such as a README.md generated there.
            // * `target/` may contain .rs files generated by build scripts when compiling
            //   the crate with cargo or rust-analyzer.
            //
            // Ideally we should just include files that are listed in `git ls-files`.
            const SKIP_PREFIXES: [&str; 3] = [".devcontainer", ".vscode", "target"];
            for skip in SKIP_PREFIXES {
                if path.starts_with(root.join(Path::new(skip))) {
                    return Ok(());
                }
            }
            f(path)
        } else {
            let context =
                || format!("Couldn't read contents of the directory at `{}`", path.display(),);
            for r in std::fs::read_dir(path).with_context(context)? {
                let entry = r.with_context(context)?;
                let path = entry.path();
                recurse(&path, root, f)?;
            }
        }
        Ok(())
    }
    recurse(path, path, f)
}

/// Get a crate's ID and parsed manifest from its path. Returns `Ok(None)` if
/// there was no Cargo.toml, or `Err(_)` for other IO errors.
fn get_vendored_crate_id(package_path: &Path) -> Result<VendoredCrate> {
    let manifest = manifest::CargoManifest::from_path(&package_path.join("Cargo.toml"))?;
    let crate_id = VendoredCrate {
        name: manifest.package.name.as_str().into(),
        version: manifest.package.version.clone(),
    };
    Ok(crate_id)
}

/// Proxy for [de]serializing epochs to/from strings. This uses the "1" or "0.1"
/// format rather than the `Display` format for `Epoch`.
#[derive(Debug, Deserialize, Serialize)]
struct EpochString(String);

impl From<Epoch> for EpochString {
    fn from(epoch: Epoch) -> Self {
        Self(epoch.to_version_string())
    }
}

impl From<EpochString> for Epoch {
    fn from(epoch: EpochString) -> Self {
        Epoch::from_version_req_str(&epoch.0)
    }
}

#[cfg(test)]
mod tests {
    use super::Epoch::*;
    use super::*;

    fn new_major(major: u64) -> Epoch {
        Major(NonZero::new(major).unwrap())
    }

    fn new_minor(minor: u64) -> Epoch {
        Minor(NonZero::new(minor).unwrap())
    }

    #[test]
    fn epoch_from_str() {
        use EpochParseError::*;
        assert_eq!(Epoch::from_str("v1"), Ok(new_major(1)));
        assert_eq!(Epoch::from_str("v2"), Ok(new_major(2)));
        assert_eq!(Epoch::from_str("v0_3"), Ok(new_minor(3)));
        assert_eq!(Epoch::from_str("0_1"), Err(BadFormat));
        assert_eq!(Epoch::from_str("v1_9"), Err(BadVersion));
        assert_eq!(Epoch::from_str("v0_0"), Err(BadVersion));
        assert_eq!(Epoch::from_str("v0_1_2"), Err(BadVersion));
        assert_eq!(Epoch::from_str("v0_0_0_0"), Err(BadFormat));
        assert_eq!(Epoch::from_str("v0_0_0"), Ok(Patch(0)));
        assert_eq!(Epoch::from_str("v0_0_1"), Ok(Patch(1)));
        assert_eq!(Epoch::from_str("v0_0_2"), Ok(Patch(2)));
        assert_eq!(Epoch::from_str("v1_0"), Err(BadVersion));
        assert_eq!(Epoch::from_str("v0_1_0"), Err(BadVersion));
        assert!(matches!(Epoch::from_str("v1_0foo"), Err(InvalidInt(_))));
        assert!(matches!(Epoch::from_str("vx_1"), Err(InvalidInt(_))));
    }

    #[test]
    fn epoch_to_string() {
        assert_eq!(new_major(1).to_string(), "v1");
        assert_eq!(new_major(2).to_string(), "v2");
        assert_eq!(new_minor(3).to_string(), "v0_3");
        assert_eq!(Patch(5).to_string(), "v0_0_5");
    }

    #[test]
    fn epoch_from_version() {
        use semver::Version;

        assert_eq!(Epoch::from_version(&Version::new(0, 0, 0)), Patch(0));
        assert_eq!(Epoch::from_version(&Version::new(0, 0, 1)), Patch(1));
        assert_eq!(Epoch::from_version(&Version::new(0, 1, 2)), new_minor(1));
        assert_eq!(Epoch::from_version(&Version::new(1, 2, 0)), new_major(1));
    }

    #[test]
    fn epoch_from_version_req_string() {
        assert_eq!(Epoch::from_version_req_str("0.0.0"), Patch(0));
        assert_eq!(Epoch::from_version_req_str("0.0.1"), Patch(1));
        assert_eq!(Epoch::from_version_req_str("0.1.0"), new_minor(1));
        assert_eq!(Epoch::from_version_req_str("1.0.0"), new_major(1));
        assert_eq!(Epoch::from_version_req_str("2.3.0"), new_major(2));
    }
}
