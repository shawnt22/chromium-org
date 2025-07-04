# Copyright 2018 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Product classes that encapsulate the interfaces for the testing targets"""

import argparse
import contextlib
import functools
import logging
from typing import List

from blinkpy.common import path_finder
from blinkpy.common.memoized import memoized
from blinkpy.web_tests.port.base import Port

_log = logging.getLogger(__name__)
IOS_VERSION = '17.0'
IOS_DEVICE = 'iPhone 14 Pro'


def do_delay_imports():
    global devil_chromium, devil_env, apk_helper
    global device_utils, webview_app, avd
    global CommandFailedError, SyncParallelizer
    # Packages here are only isolated when running test for Android
    # This import adds `devil` to `sys.path`.
    path_finder.add_build_android_to_sys_path()
    import devil_chromium
    from devil import devil_env
    from devil.android import apk_helper
    from devil.android import device_utils
    from devil.android.device_errors import CommandFailedError
    from devil.android.tools import webview_app
    from devil.utils.parallelizer import SyncParallelizer
    from pylib.local.emulator import avd


@memoized
def make_product_registry():
    """Create a mapping from all product names (including aliases) to their
    respective classes.
    """
    product_registry = {}
    product_classes = [
        Chrome,
        HeadlessShell,
        ChromeiOS,
        ChromeAndroid,
        WebView,
    ]
    for product_cls in product_classes:
        names = [product_cls.name] + product_cls.aliases
        product_registry.update((name, product_cls) for name in names)
    return product_registry


class Product:
    """A product (browser or browser component) that can run web platform tests.

    Attributes:
        name (str): The official wpt-accepted name of this product.
        aliases (list[str]): Human-friendly aliases for the official name.
    """
    name = ''
    aliases = []

    def __init__(self, port: Port, options):
        self._port = port
        self._host = port.host
        self._options = options
        self._tasks = contextlib.ExitStack()

    @contextlib.contextmanager
    def test_env(self):
        """Set up and clean up the test environment."""
        with self._tasks:
            yield

    def update_runner_options(self, options: argparse.Namespace):
        options.processes = self.processes
        # pylint: disable=assignment-from-none
        options.browser_version = self.get_version()
        if self._options.stable:
            options.channel = 'stable'
            # Don't use a local trunk build of `chromedriver`, whose major
            # version might be ahead of stable. Instead, download a compatible
            # `chromedriver` version from Chrome for Testing.
            options.install_webdriver = True
            options.yes = True
        else:
            options.webdriver_binary = (self._options.webdriver_binary
                                        or self.webdriver_binary)
        options.webdriver_args.extend(self.additional_webdriver_args())

    @functools.cached_property
    def processes(self) -> int:
        if self._options.child_processes:
            return self._options.child_processes
        elif self._options.wrapper:
            _log.info('Defaulting to 1 worker because of debugging option '
                      '`--wrapper`')
            return 1
        else:
            return self._port.default_child_processes()

    def additional_webdriver_args(self):
        """Additional webdriver parameters for the product"""
        return []

    def get_version(self):
        """Get the product version, if available."""
        return None

    @property
    def webdriver_binary(self):
        if self._host.platform.is_win():
            path = 'chromedriver.exe'
        else:
            path = 'chromedriver'  #linux and mac
        return self._port.build_path(path)


class DesktopProduct(Product):

    def update_runner_options(self, options: argparse.Namespace):
        super().update_runner_options(options)
        options.binary = self._port.path_to_driver()
        options.binary_args.extend(self.additional_binary_args())

    def additional_binary_args(self) -> List[str]:
        # Base args applicable to all embedders.
        args = [
            # Expose the non-standard `window.gc()` for `wpt_internal/` tests.
            '--js-flags=--expose-gc',
            # Disable overlay scrollbar fadeout for consistent screenshots.
            '--disable-features=ScrollbarAnimations',
        ]
        fs = self._host.filesystem
        if (self._options.wrapper
                and fs.basename(self._options.wrapper[0]) == 'rr'):
            debug_args = [
                '--no-sandbox',
                '--disable-hang-monitor',
            ]
            args.extend(debug_args)
            _log.info(f'Running {self.name!r} with {" ".join(debug_args)!r} '
                      'because of debugging option `--wrapper=rr`')
        if self._options.wrapper and self._host.platform.is_win():
            # The adapter will generate a batch file wrapping the browser
            # command. Because `cmd.exe` doesn't have an equivalent of Unix's
            # `exec`, there will be a real process in between chromedriver and
            # the browser process. Because the batch file doesn't know how to
            # relay the file handles it receives to the browser, the default
            # `--remote-debugging-pipe` won't work. Use any free network port
            # instead for chromedriver-browser traffic.
            args.append('--remote-debugging-port=0')
        return args


class Chrome(DesktopProduct):
    name = 'chrome'


class HeadlessShell(DesktopProduct):
    name = 'headless_shell'

    def additional_binary_args(self):
        rv = [
            *super().additional_binary_args(),
            "--canvas-2d-layers",
            '--enable-bfcache',
            '--enable-field-trial-config',
            '--force-reporting-destination-attested',
            # `headless_shell` doesn't send the `Accept-Language` header by
            # default, so set an arbitrary one that some tests expect.
            '--accept-lang=en-US,en',
        ]
        if self._port.operating_system() != 'linux':
            rv.append('--disable-site-isolation-trials')
        return rv


class ChromeiOS(Product):
    name = 'chrome_ios'

    def get_version(self):
        # TODO(crbug.com/374199289): The build directory must be plumbed to
        # `run_cwt_chromedriver_wrapper.py --build-dir` to find the version in
        # an `Info.plist` file, but the directory isn't known to the `wpt run`
        # code [0] because "build directory" is a Chromium-specific concept.
        # For now, explicitly find the version in this Chromium-side wrapper,
        # which overrides [0].
        #
        # [0]: https://github.com/web-platform-tests/wpt/blob/b6027ab/tools/wpt/browser.py#L1558
        return self._host.executive.run_command([
            self.webdriver_binary,
            f'--build-dir={self._port.build_path()}',
            '--version',
        ]).strip()

    @property
    def processes(self) -> int:
        return 1

    @property
    def webdriver_binary(self) -> str:
        return self._port._path_finder.path_from_chromium_base(
            'ios', 'chrome', 'test', 'wpt', 'tools',
            'run_cwt_chromedriver_wrapper.py')

    def additional_webdriver_args(self):
        # Set up xcode log output dir.
        output_dir = self._host.filesystem.join(
            self._port.artifacts_directory(), 'xcode-output')
        return [
            f'--out-dir={output_dir}',
            f'--os={IOS_VERSION}',
            f'--device={IOS_DEVICE}',
            f'--build-dir={self._port.build_path()}',
        ]


class ChromeAndroidBase(Product):
    def __init__(self, port, options):
        do_delay_imports()
        super().__init__(port, options)
        if options.browser_apk:
            self.browser_apk = options.browser_apk
        else:
            self.browser_apk = self.default_browser_apk
        self.adb_binary = devil_env.config.FetchPath('adb')  # pylint: disable=undefined-variable;
        self.devices = []

    @contextlib.contextmanager
    def _install_apk(self, device, path):
        """Helper context manager for ensuring a device uninstalls an APK."""
        device.Install(path)
        try:
            yield
        finally:
            device.Uninstall(path)

    @contextlib.contextmanager
    def _install_chrome_stable(self, device):
        install_script = self._port._path_finder.path_from_chromium_base(
            'clank', 'bin', 'install_chrome.py')
        self._host.executive.run_command([
            install_script,
            '--serial',
            device.serial,
            '--channel',
            'stable',
            '--adb',
            self.adb_binary,
            '--package',
            'TrichromeChromeGoogle6432',
        ])
        try:
            yield
        finally:
            # Do nothing as install_chrome.py does not uninstall.
            pass

    @contextlib.contextmanager
    def _install_incremental_apk(self, device):
        """Helper context manager for ensuring a device uninstalls incremental
        APK."""
        install_script = self._port.build_path('bin/chrome_public_apk')
        self._host.executive.run_command(
            [install_script, 'install', '--device', device])
        try:
            yield
        finally:
            self._host.executive.run_command(
                [install_script, 'uninstall', '--device', device])

    @contextlib.contextmanager
    def get_devices(self):
        instances = []
        try:
            if self._options.avd_config:
                _log.info(
                    f'Installing emulator from {self._options.avd_config}')
                config = avd.AvdConfig(self._options.avd_config)  # pylint: disable=undefined-variable;
                config.Install()

                # use '--child-processes' to decide how many emulators to launch
                for _ in range(max(self.processes, 1)):
                    instance = config.CreateInstance()
                    instances.append(instance)

                SyncParallelizer(instances).Start(  # pylint: disable=undefined-variable;
                    writable_system=True,
                    window=self._options.emulator_window)

            #TODO(weizhong): when choose device, make sure abi matches with target
            yield device_utils.DeviceUtils.HealthyDevices()  # pylint: disable=undefined-variable;
        finally:
            SyncParallelizer(instances).Stop()  # pylint: disable=undefined-variable;

    @contextlib.contextmanager
    def test_env(self):
        with super().test_env():
            devil_chromium.Initialize(adb_path=self.adb_binary)  # pylint: disable=undefined-variable;
            self.devices = self._tasks.enter_context(self.get_devices())
            if not self.devices:
                raise Exception('No devices attached to this host. '
                                "Make sure to provide '--avd-config' "
                                'if using only emulators.')

            if not self._options.no_install:
                self.provision_devices()
            yield

    def update_runner_options(self, options: argparse.Namespace):
        super().update_runner_options(options)
        options.adb_binary = self.adb_binary
        options.device_serial = [device.serial for device in self.devices]
        options.package_name = self.get_browser_package_name()

    def get_version(self):
        version_provider = self.get_version_provider_package_name()
        if self.devices and version_provider:
            # Assume devices are identically provisioned, so select any.
            device = self.devices[0]
            try:
                version = device.GetApplicationVersion(version_provider)
                _log.info('Product version: %s %s (package: %r)', self.name,
                          version, version_provider)
                return version
            except CommandFailedError:  # pylint: disable=undefined-variable;
                _log.warning('Failed to retrieve version of %s (package: %r)',
                             self.name, version_provider)
        return None

    @property
    def processes(self) -> int:
        return 1

    @property
    def webdriver_binary(self):
        return self._port.build_path('clang_x64', 'chromedriver')

    def get_browser_package_name(self):
        """Get the name of the package to run tests against.

        For WebView, this package is the shell.

        Returns:
            Optional[str]: The name of a package installed on the devices or
                `None` to use wpt's best guess of the runnable package.

        See Also:
            https://github.com/web-platform-tests/wpt/blob/merge_pr_33203/tools/wpt/browser.py#L867-L924
        """
        if self._options.stable:
            return 'com.android.chrome'
        if self.browser_apk:
            # pylint: disable=undefined-variable;
            with contextlib.suppress(apk_helper.ApkHelperError):
                return apk_helper.GetPackageName(self.browser_apk)
        return None

    def get_version_provider_package_name(self):
        """Get the name of the package containing the product version.

        Some Android products are made up of multiple packages with decoupled
        "versionName" fields. This method identifies the package whose
        "versionName" should be consider the product's version.

        Returns:
            Optional[str]: The name of a package installed on the devices or
                `None` to use wpt's best guess of the version.

        See Also:
            https://github.com/web-platform-tests/wpt/blob/merge_pr_33203/tools/wpt/run.py#L810-L816
            https://github.com/web-platform-tests/wpt/blob/merge_pr_33203/tools/wpt/browser.py#L850-L924
        """
        # Assume the product is a single APK.
        return self.get_browser_package_name()

    def provision_devices(self):
        """Provisions a set of Android devices in parallel."""
        contexts = [self._provision_device(device) for device in self.devices]
        self._tasks.enter_context(SyncParallelizer(contexts))  # pylint: disable=undefined-variable;

    @contextlib.contextmanager
    def _provision_device(self, device):
        """Provision a single Android device for a test.

        This method will be executed in parallel on all devices, so
        it is crucial that it is thread safe.
        """
        with contextlib.ExitStack() as exit_stack:
            for apk in self._options.additional_apk:
                exit_stack.enter_context(self._install_apk(device, apk))
            if self._options.stable:
                install_context_manager = self._install_chrome_stable(device)
            elif self._port._build_is_incremental_install():
                install_context_manager = self._install_incremental_apk(device)
            else:
                install_context_manager = self._install_apk(
                    device, self.browser_apk)
            exit_stack.enter_context(cm=install_context_manager)
            _log.info('Provisioned device (serial: %s)', device.serial)
            yield


class WebView(ChromeAndroidBase):
    name = 'android_webview'
    aliases = ['webview']

    def __init__(self, port, options):
        super().__init__(port, options)
        if options.webview_provider:
            self.webview_provider = options.webview_provider
        else:
            self.webview_provider = self.default_webview_provider

    @property
    def default_browser_apk(self):
        return self._port.build_path('apks', 'SystemWebViewShell.apk')

    @property
    def default_webview_provider(self):
        return self._port.build_path('apks', 'SystemWebView.apk')

    def _install_webview(self, device):
        # Prioritize local builds.
        # pylint: disable=undefined-variable;
        return webview_app.UseWebViewProvider(device, self.webview_provider)

    def get_browser_package_name(self):
        return (super().get_browser_package_name()
                or 'org.chromium.webview_shell')

    def get_version_provider_package_name(self):
        # Use the version from the webview provider, not the shell, since the
        # provider is distributed to end users. The shell is developer-facing,
        # so its version is usually not actively updated.
        # pylint: disable=undefined-variable;
        with contextlib.suppress(apk_helper.ApkHelperError):
            return apk_helper.GetPackageName(self.webview_provider)

    @contextlib.contextmanager
    def _provision_device(self, device):
        # WebView installation must execute after device provisioning
        # as the installation might depends on additional packages.
        with super()._provision_device(device), self._install_webview(device):
            yield


class ChromeAndroid(ChromeAndroidBase):
    name = 'chrome_android'
    aliases = ['clank']

    @property
    def default_browser_apk(self):
        if self._port._build_is_incremental_install():
            return self._port.build_path('apks',
                                         'ChromePublic_incremental.apk')
        return self._port.build_path('apks', 'ChromePublic.apk')
