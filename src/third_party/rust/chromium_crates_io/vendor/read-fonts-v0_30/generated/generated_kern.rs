// THIS FILE IS AUTOGENERATED.
// Any changes to this file will be overwritten.
// For more information about how codegen works, see font-codegen/README.md

#[allow(unused_imports)]
use crate::codegen_prelude::*;

/// The OpenType [kerning](https://learn.microsoft.com/en-us/typography/opentype/spec/kern) table.
#[derive(Debug, Clone, Copy)]
#[doc(hidden)]
pub struct OtKernMarker {
    subtable_data_byte_len: usize,
}

impl OtKernMarker {
    pub fn version_byte_range(&self) -> Range<usize> {
        let start = 0;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn n_tables_byte_range(&self) -> Range<usize> {
        let start = self.version_byte_range().end;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn subtable_data_byte_range(&self) -> Range<usize> {
        let start = self.n_tables_byte_range().end;
        start..start + self.subtable_data_byte_len
    }
}

impl MinByteRange for OtKernMarker {
    fn min_byte_range(&self) -> Range<usize> {
        0..self.subtable_data_byte_range().end
    }
}

impl<'a> FontRead<'a> for OtKern<'a> {
    fn read(data: FontData<'a>) -> Result<Self, ReadError> {
        let mut cursor = data.cursor();
        cursor.advance::<u16>();
        cursor.advance::<u16>();
        let subtable_data_byte_len = cursor.remaining_bytes() / u8::RAW_BYTE_LEN * u8::RAW_BYTE_LEN;
        cursor.advance_by(subtable_data_byte_len);
        cursor.finish(OtKernMarker {
            subtable_data_byte_len,
        })
    }
}

/// The OpenType [kerning](https://learn.microsoft.com/en-us/typography/opentype/spec/kern) table.
pub type OtKern<'a> = TableRef<'a, OtKernMarker>;

#[allow(clippy::needless_lifetimes)]
impl<'a> OtKern<'a> {
    /// Table version number—set to 0.
    pub fn version(&self) -> u16 {
        let range = self.shape.version_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// Number of subtables in the kerning table.
    pub fn n_tables(&self) -> u16 {
        let range = self.shape.n_tables_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// Data for subtables, immediately following the header.
    pub fn subtable_data(&self) -> &'a [u8] {
        let range = self.shape.subtable_data_byte_range();
        self.data.read_array(range).unwrap()
    }
}

#[cfg(feature = "experimental_traverse")]
impl<'a> SomeTable<'a> for OtKern<'a> {
    fn type_name(&self) -> &str {
        "OtKern"
    }
    fn get_field(&self, idx: usize) -> Option<Field<'a>> {
        match idx {
            0usize => Some(Field::new("version", self.version())),
            1usize => Some(Field::new("n_tables", self.n_tables())),
            2usize => Some(Field::new("subtable_data", self.subtable_data())),
            _ => None,
        }
    }
}

#[cfg(feature = "experimental_traverse")]
#[allow(clippy::needless_lifetimes)]
impl<'a> std::fmt::Debug for OtKern<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        (self as &dyn SomeTable<'a>).fmt(f)
    }
}

/// The Apple Advanced Typography [kerning](https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6kern.html) table.
#[derive(Debug, Clone, Copy)]
#[doc(hidden)]
pub struct AatKernMarker {
    subtable_data_byte_len: usize,
}

impl AatKernMarker {
    pub fn version_byte_range(&self) -> Range<usize> {
        let start = 0;
        start..start + MajorMinor::RAW_BYTE_LEN
    }

    pub fn n_tables_byte_range(&self) -> Range<usize> {
        let start = self.version_byte_range().end;
        start..start + u32::RAW_BYTE_LEN
    }

    pub fn subtable_data_byte_range(&self) -> Range<usize> {
        let start = self.n_tables_byte_range().end;
        start..start + self.subtable_data_byte_len
    }
}

impl MinByteRange for AatKernMarker {
    fn min_byte_range(&self) -> Range<usize> {
        0..self.subtable_data_byte_range().end
    }
}

impl<'a> FontRead<'a> for AatKern<'a> {
    fn read(data: FontData<'a>) -> Result<Self, ReadError> {
        let mut cursor = data.cursor();
        cursor.advance::<MajorMinor>();
        cursor.advance::<u32>();
        let subtable_data_byte_len = cursor.remaining_bytes() / u8::RAW_BYTE_LEN * u8::RAW_BYTE_LEN;
        cursor.advance_by(subtable_data_byte_len);
        cursor.finish(AatKernMarker {
            subtable_data_byte_len,
        })
    }
}

/// The Apple Advanced Typography [kerning](https://developer.apple.com/fonts/TrueType-Reference-Manual/RM06/Chap6kern.html) table.
pub type AatKern<'a> = TableRef<'a, AatKernMarker>;

#[allow(clippy::needless_lifetimes)]
impl<'a> AatKern<'a> {
    /// The version number of the kerning table (0x00010000 for the current version).
    pub fn version(&self) -> MajorMinor {
        let range = self.shape.version_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// The number of subtables included in the kerning table.
    pub fn n_tables(&self) -> u32 {
        let range = self.shape.n_tables_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// Data for subtables, immediately following the header.    
    pub fn subtable_data(&self) -> &'a [u8] {
        let range = self.shape.subtable_data_byte_range();
        self.data.read_array(range).unwrap()
    }
}

#[cfg(feature = "experimental_traverse")]
impl<'a> SomeTable<'a> for AatKern<'a> {
    fn type_name(&self) -> &str {
        "AatKern"
    }
    fn get_field(&self, idx: usize) -> Option<Field<'a>> {
        match idx {
            0usize => Some(Field::new("version", self.version())),
            1usize => Some(Field::new("n_tables", self.n_tables())),
            2usize => Some(Field::new("subtable_data", self.subtable_data())),
            _ => None,
        }
    }
}

#[cfg(feature = "experimental_traverse")]
#[allow(clippy::needless_lifetimes)]
impl<'a> std::fmt::Debug for AatKern<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        (self as &dyn SomeTable<'a>).fmt(f)
    }
}

/// A subtable in an OT `kern` table.
#[derive(Debug, Clone, Copy)]
#[doc(hidden)]
pub struct OtSubtableMarker {
    data_byte_len: usize,
}

impl OtSubtableMarker {
    pub fn version_byte_range(&self) -> Range<usize> {
        let start = 0;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn length_byte_range(&self) -> Range<usize> {
        let start = self.version_byte_range().end;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn coverage_byte_range(&self) -> Range<usize> {
        let start = self.length_byte_range().end;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn data_byte_range(&self) -> Range<usize> {
        let start = self.coverage_byte_range().end;
        start..start + self.data_byte_len
    }
}

impl MinByteRange for OtSubtableMarker {
    fn min_byte_range(&self) -> Range<usize> {
        0..self.data_byte_range().end
    }
}

impl<'a> FontRead<'a> for OtSubtable<'a> {
    fn read(data: FontData<'a>) -> Result<Self, ReadError> {
        let mut cursor = data.cursor();
        cursor.advance::<u16>();
        cursor.advance::<u16>();
        cursor.advance::<u16>();
        let data_byte_len = cursor.remaining_bytes() / u8::RAW_BYTE_LEN * u8::RAW_BYTE_LEN;
        cursor.advance_by(data_byte_len);
        cursor.finish(OtSubtableMarker { data_byte_len })
    }
}

/// A subtable in an OT `kern` table.
pub type OtSubtable<'a> = TableRef<'a, OtSubtableMarker>;

#[allow(clippy::needless_lifetimes)]
impl<'a> OtSubtable<'a> {
    /// Kern subtable version number-- set to 0.
    pub fn version(&self) -> u16 {
        let range = self.shape.version_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// The length of this subtable in bytes, including this header.
    pub fn length(&self) -> u16 {
        let range = self.shape.length_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// Circumstances under which this table is used.
    pub fn coverage(&self) -> u16 {
        let range = self.shape.coverage_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// Subtable specific data.
    pub fn data(&self) -> &'a [u8] {
        let range = self.shape.data_byte_range();
        self.data.read_array(range).unwrap()
    }
}

#[cfg(feature = "experimental_traverse")]
impl<'a> SomeTable<'a> for OtSubtable<'a> {
    fn type_name(&self) -> &str {
        "OtSubtable"
    }
    fn get_field(&self, idx: usize) -> Option<Field<'a>> {
        match idx {
            0usize => Some(Field::new("version", self.version())),
            1usize => Some(Field::new("length", self.length())),
            2usize => Some(Field::new("coverage", self.coverage())),
            3usize => Some(Field::new("data", self.data())),
            _ => None,
        }
    }
}

#[cfg(feature = "experimental_traverse")]
#[allow(clippy::needless_lifetimes)]
impl<'a> std::fmt::Debug for OtSubtable<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        (self as &dyn SomeTable<'a>).fmt(f)
    }
}

/// A subtable in an AAT `kern` table.
#[derive(Debug, Clone, Copy)]
#[doc(hidden)]
pub struct AatSubtableMarker {
    data_byte_len: usize,
}

impl AatSubtableMarker {
    pub fn length_byte_range(&self) -> Range<usize> {
        let start = 0;
        start..start + u32::RAW_BYTE_LEN
    }

    pub fn coverage_byte_range(&self) -> Range<usize> {
        let start = self.length_byte_range().end;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn tuple_index_byte_range(&self) -> Range<usize> {
        let start = self.coverage_byte_range().end;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn data_byte_range(&self) -> Range<usize> {
        let start = self.tuple_index_byte_range().end;
        start..start + self.data_byte_len
    }
}

impl MinByteRange for AatSubtableMarker {
    fn min_byte_range(&self) -> Range<usize> {
        0..self.data_byte_range().end
    }
}

impl<'a> FontRead<'a> for AatSubtable<'a> {
    fn read(data: FontData<'a>) -> Result<Self, ReadError> {
        let mut cursor = data.cursor();
        cursor.advance::<u32>();
        cursor.advance::<u16>();
        cursor.advance::<u16>();
        let data_byte_len = cursor.remaining_bytes() / u8::RAW_BYTE_LEN * u8::RAW_BYTE_LEN;
        cursor.advance_by(data_byte_len);
        cursor.finish(AatSubtableMarker { data_byte_len })
    }
}

/// A subtable in an AAT `kern` table.
pub type AatSubtable<'a> = TableRef<'a, AatSubtableMarker>;

#[allow(clippy::needless_lifetimes)]
impl<'a> AatSubtable<'a> {
    /// The length of this subtable in bytes, including this header.
    pub fn length(&self) -> u32 {
        let range = self.shape.length_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// Circumstances under which this table is used.
    pub fn coverage(&self) -> u16 {
        let range = self.shape.coverage_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// The tuple index (used for variations fonts). This value specifies which tuple this subtable covers.
    pub fn tuple_index(&self) -> u16 {
        let range = self.shape.tuple_index_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// Subtable specific data.
    pub fn data(&self) -> &'a [u8] {
        let range = self.shape.data_byte_range();
        self.data.read_array(range).unwrap()
    }
}

#[cfg(feature = "experimental_traverse")]
impl<'a> SomeTable<'a> for AatSubtable<'a> {
    fn type_name(&self) -> &str {
        "AatSubtable"
    }
    fn get_field(&self, idx: usize) -> Option<Field<'a>> {
        match idx {
            0usize => Some(Field::new("length", self.length())),
            1usize => Some(Field::new("coverage", self.coverage())),
            2usize => Some(Field::new("tuple_index", self.tuple_index())),
            3usize => Some(Field::new("data", self.data())),
            _ => None,
        }
    }
}

#[cfg(feature = "experimental_traverse")]
#[allow(clippy::needless_lifetimes)]
impl<'a> std::fmt::Debug for AatSubtable<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        (self as &dyn SomeTable<'a>).fmt(f)
    }
}

/// The type 0 `kern` subtable.
#[derive(Debug, Clone, Copy)]
#[doc(hidden)]
pub struct Subtable0Marker {
    pairs_byte_len: usize,
}

impl Subtable0Marker {
    pub fn n_pairs_byte_range(&self) -> Range<usize> {
        let start = 0;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn search_range_byte_range(&self) -> Range<usize> {
        let start = self.n_pairs_byte_range().end;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn entry_selector_byte_range(&self) -> Range<usize> {
        let start = self.search_range_byte_range().end;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn range_shift_byte_range(&self) -> Range<usize> {
        let start = self.entry_selector_byte_range().end;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn pairs_byte_range(&self) -> Range<usize> {
        let start = self.range_shift_byte_range().end;
        start..start + self.pairs_byte_len
    }
}

impl MinByteRange for Subtable0Marker {
    fn min_byte_range(&self) -> Range<usize> {
        0..self.pairs_byte_range().end
    }
}

impl<'a> FontRead<'a> for Subtable0<'a> {
    fn read(data: FontData<'a>) -> Result<Self, ReadError> {
        let mut cursor = data.cursor();
        let n_pairs: u16 = cursor.read()?;
        cursor.advance::<u16>();
        cursor.advance::<u16>();
        cursor.advance::<u16>();
        let pairs_byte_len = (n_pairs as usize)
            .checked_mul(Subtable0Pair::RAW_BYTE_LEN)
            .ok_or(ReadError::OutOfBounds)?;
        cursor.advance_by(pairs_byte_len);
        cursor.finish(Subtable0Marker { pairs_byte_len })
    }
}

/// The type 0 `kern` subtable.
pub type Subtable0<'a> = TableRef<'a, Subtable0Marker>;

#[allow(clippy::needless_lifetimes)]
impl<'a> Subtable0<'a> {
    /// The number of kerning pairs in this subtable.
    pub fn n_pairs(&self) -> u16 {
        let range = self.shape.n_pairs_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// The largest power of two less than or equal to the value of nPairs, multiplied by the size in bytes of an entry in the subtable.
    pub fn search_range(&self) -> u16 {
        let range = self.shape.search_range_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// This is calculated as log2 of the largest power of two less than or equal to the value of nPairs. This value indicates how many iterations of the search loop have to be made. For example, in a list of eight items, there would be three iterations of the loop.
    pub fn entry_selector(&self) -> u16 {
        let range = self.shape.entry_selector_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// The value of nPairs minus the largest power of two less than or equal to nPairs. This is multiplied by the size in bytes of an entry in the table.
    pub fn range_shift(&self) -> u16 {
        let range = self.shape.range_shift_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// Kerning records.
    pub fn pairs(&self) -> &'a [Subtable0Pair] {
        let range = self.shape.pairs_byte_range();
        self.data.read_array(range).unwrap()
    }
}

#[cfg(feature = "experimental_traverse")]
impl<'a> SomeTable<'a> for Subtable0<'a> {
    fn type_name(&self) -> &str {
        "Subtable0"
    }
    fn get_field(&self, idx: usize) -> Option<Field<'a>> {
        match idx {
            0usize => Some(Field::new("n_pairs", self.n_pairs())),
            1usize => Some(Field::new("search_range", self.search_range())),
            2usize => Some(Field::new("entry_selector", self.entry_selector())),
            3usize => Some(Field::new("range_shift", self.range_shift())),
            4usize => Some(Field::new(
                "pairs",
                traversal::FieldType::array_of_records(
                    stringify!(Subtable0Pair),
                    self.pairs(),
                    self.offset_data(),
                ),
            )),
            _ => None,
        }
    }
}

#[cfg(feature = "experimental_traverse")]
#[allow(clippy::needless_lifetimes)]
impl<'a> std::fmt::Debug for Subtable0<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        (self as &dyn SomeTable<'a>).fmt(f)
    }
}

/// Class table for the type 2 `kern` subtable.
#[derive(Debug, Clone, Copy)]
#[doc(hidden)]
pub struct Subtable2ClassTableMarker {
    offsets_byte_len: usize,
}

impl Subtable2ClassTableMarker {
    pub fn first_glyph_byte_range(&self) -> Range<usize> {
        let start = 0;
        start..start + GlyphId16::RAW_BYTE_LEN
    }

    pub fn n_glyphs_byte_range(&self) -> Range<usize> {
        let start = self.first_glyph_byte_range().end;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn offsets_byte_range(&self) -> Range<usize> {
        let start = self.n_glyphs_byte_range().end;
        start..start + self.offsets_byte_len
    }
}

impl MinByteRange for Subtable2ClassTableMarker {
    fn min_byte_range(&self) -> Range<usize> {
        0..self.offsets_byte_range().end
    }
}

impl<'a> FontRead<'a> for Subtable2ClassTable<'a> {
    fn read(data: FontData<'a>) -> Result<Self, ReadError> {
        let mut cursor = data.cursor();
        cursor.advance::<GlyphId16>();
        let n_glyphs: u16 = cursor.read()?;
        let offsets_byte_len = (n_glyphs as usize)
            .checked_mul(u16::RAW_BYTE_LEN)
            .ok_or(ReadError::OutOfBounds)?;
        cursor.advance_by(offsets_byte_len);
        cursor.finish(Subtable2ClassTableMarker { offsets_byte_len })
    }
}

/// Class table for the type 2 `kern` subtable.
pub type Subtable2ClassTable<'a> = TableRef<'a, Subtable2ClassTableMarker>;

#[allow(clippy::needless_lifetimes)]
impl<'a> Subtable2ClassTable<'a> {
    /// First glyph in class range.
    pub fn first_glyph(&self) -> GlyphId16 {
        let range = self.shape.first_glyph_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// Number of glyph in class range.
    pub fn n_glyphs(&self) -> u16 {
        let range = self.shape.n_glyphs_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// The offsets array for all of the glyphs in the range.
    pub fn offsets(&self) -> &'a [BigEndian<u16>] {
        let range = self.shape.offsets_byte_range();
        self.data.read_array(range).unwrap()
    }
}

#[cfg(feature = "experimental_traverse")]
impl<'a> SomeTable<'a> for Subtable2ClassTable<'a> {
    fn type_name(&self) -> &str {
        "Subtable2ClassTable"
    }
    fn get_field(&self, idx: usize) -> Option<Field<'a>> {
        match idx {
            0usize => Some(Field::new("first_glyph", self.first_glyph())),
            1usize => Some(Field::new("n_glyphs", self.n_glyphs())),
            2usize => Some(Field::new("offsets", self.offsets())),
            _ => None,
        }
    }
}

#[cfg(feature = "experimental_traverse")]
#[allow(clippy::needless_lifetimes)]
impl<'a> std::fmt::Debug for Subtable2ClassTable<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        (self as &dyn SomeTable<'a>).fmt(f)
    }
}

/// The type 3 'kern' subtable.
#[derive(Debug, Clone, Copy)]
#[doc(hidden)]
pub struct Subtable3Marker {
    kern_value_byte_len: usize,
    left_class_byte_len: usize,
    right_class_byte_len: usize,
    kern_index_byte_len: usize,
}

impl Subtable3Marker {
    pub fn glyph_count_byte_range(&self) -> Range<usize> {
        let start = 0;
        start..start + u16::RAW_BYTE_LEN
    }

    pub fn kern_value_count_byte_range(&self) -> Range<usize> {
        let start = self.glyph_count_byte_range().end;
        start..start + u8::RAW_BYTE_LEN
    }

    pub fn left_class_count_byte_range(&self) -> Range<usize> {
        let start = self.kern_value_count_byte_range().end;
        start..start + u8::RAW_BYTE_LEN
    }

    pub fn right_class_count_byte_range(&self) -> Range<usize> {
        let start = self.left_class_count_byte_range().end;
        start..start + u8::RAW_BYTE_LEN
    }

    pub fn flags_byte_range(&self) -> Range<usize> {
        let start = self.right_class_count_byte_range().end;
        start..start + u8::RAW_BYTE_LEN
    }

    pub fn kern_value_byte_range(&self) -> Range<usize> {
        let start = self.flags_byte_range().end;
        start..start + self.kern_value_byte_len
    }

    pub fn left_class_byte_range(&self) -> Range<usize> {
        let start = self.kern_value_byte_range().end;
        start..start + self.left_class_byte_len
    }

    pub fn right_class_byte_range(&self) -> Range<usize> {
        let start = self.left_class_byte_range().end;
        start..start + self.right_class_byte_len
    }

    pub fn kern_index_byte_range(&self) -> Range<usize> {
        let start = self.right_class_byte_range().end;
        start..start + self.kern_index_byte_len
    }
}

impl MinByteRange for Subtable3Marker {
    fn min_byte_range(&self) -> Range<usize> {
        0..self.kern_index_byte_range().end
    }
}

impl<'a> FontRead<'a> for Subtable3<'a> {
    fn read(data: FontData<'a>) -> Result<Self, ReadError> {
        let mut cursor = data.cursor();
        let glyph_count: u16 = cursor.read()?;
        let kern_value_count: u8 = cursor.read()?;
        let left_class_count: u8 = cursor.read()?;
        let right_class_count: u8 = cursor.read()?;
        cursor.advance::<u8>();
        let kern_value_byte_len = (kern_value_count as usize)
            .checked_mul(i16::RAW_BYTE_LEN)
            .ok_or(ReadError::OutOfBounds)?;
        cursor.advance_by(kern_value_byte_len);
        let left_class_byte_len = (glyph_count as usize)
            .checked_mul(u8::RAW_BYTE_LEN)
            .ok_or(ReadError::OutOfBounds)?;
        cursor.advance_by(left_class_byte_len);
        let right_class_byte_len = (glyph_count as usize)
            .checked_mul(u8::RAW_BYTE_LEN)
            .ok_or(ReadError::OutOfBounds)?;
        cursor.advance_by(right_class_byte_len);
        let kern_index_byte_len =
            (transforms::add_multiply(left_class_count, 0_usize, right_class_count))
                .checked_mul(u8::RAW_BYTE_LEN)
                .ok_or(ReadError::OutOfBounds)?;
        cursor.advance_by(kern_index_byte_len);
        cursor.finish(Subtable3Marker {
            kern_value_byte_len,
            left_class_byte_len,
            right_class_byte_len,
            kern_index_byte_len,
        })
    }
}

/// The type 3 'kern' subtable.
pub type Subtable3<'a> = TableRef<'a, Subtable3Marker>;

#[allow(clippy::needless_lifetimes)]
impl<'a> Subtable3<'a> {
    /// The number of glyphs in this font.
    pub fn glyph_count(&self) -> u16 {
        let range = self.shape.glyph_count_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// The number of kerning values.
    pub fn kern_value_count(&self) -> u8 {
        let range = self.shape.kern_value_count_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// The number of left-hand classes.
    pub fn left_class_count(&self) -> u8 {
        let range = self.shape.left_class_count_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// The number of right-hand classes.
    pub fn right_class_count(&self) -> u8 {
        let range = self.shape.right_class_count_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// Set to zero (reserved for future use).
    pub fn flags(&self) -> u8 {
        let range = self.shape.flags_byte_range();
        self.data.read_at(range.start).unwrap()
    }

    /// The kerning values.
    pub fn kern_value(&self) -> &'a [BigEndian<i16>] {
        let range = self.shape.kern_value_byte_range();
        self.data.read_array(range).unwrap()
    }

    /// The left-hand classes.
    pub fn left_class(&self) -> &'a [u8] {
        let range = self.shape.left_class_byte_range();
        self.data.read_array(range).unwrap()
    }

    /// The right-hand classes.
    pub fn right_class(&self) -> &'a [u8] {
        let range = self.shape.right_class_byte_range();
        self.data.read_array(range).unwrap()
    }

    /// The indices into the kernValue array.
    pub fn kern_index(&self) -> &'a [u8] {
        let range = self.shape.kern_index_byte_range();
        self.data.read_array(range).unwrap()
    }
}

#[cfg(feature = "experimental_traverse")]
impl<'a> SomeTable<'a> for Subtable3<'a> {
    fn type_name(&self) -> &str {
        "Subtable3"
    }
    fn get_field(&self, idx: usize) -> Option<Field<'a>> {
        match idx {
            0usize => Some(Field::new("glyph_count", self.glyph_count())),
            1usize => Some(Field::new("kern_value_count", self.kern_value_count())),
            2usize => Some(Field::new("left_class_count", self.left_class_count())),
            3usize => Some(Field::new("right_class_count", self.right_class_count())),
            4usize => Some(Field::new("flags", self.flags())),
            5usize => Some(Field::new("kern_value", self.kern_value())),
            6usize => Some(Field::new("left_class", self.left_class())),
            7usize => Some(Field::new("right_class", self.right_class())),
            8usize => Some(Field::new("kern_index", self.kern_index())),
            _ => None,
        }
    }
}

#[cfg(feature = "experimental_traverse")]
#[allow(clippy::needless_lifetimes)]
impl<'a> std::fmt::Debug for Subtable3<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        (self as &dyn SomeTable<'a>).fmt(f)
    }
}
