// Extracted from src/type-layout.md:221
#![allow(unused)]
fn main() {
    /// A field of a struct.
    #[derive(Debug)]
    struct Field {
        alignment: usize,
        size: usize,
    }
    /// Layout of user-defined structs.
    #[derive(Debug)]
    struct MockLayout {
        /// Fields stored in declaration order.
        fields: Vec<Field>,
        /// Offset of each field from the start of the struct.
        field_offsets: Vec<usize>,
        /// Overall alignment.
        alignment: usize,
        /// Overall size.
        size: usize,
    }
    
    impl MockLayout {
        /// Returns the amount of padding needed after `offset` to ensure that the
        /// following address will be aligned to `alignment`.
        fn padding_needed_for(offset: usize, alignment: usize) -> usize {
            let misalignment = offset % alignment;
            if misalignment > 0 {
                // Round up to next multiple of `alignment`.
                alignment - misalignment
            } else {
                // Already a multiple of `alignment`.
                0
            }
        }
    
        /// Fields must be in declaration order. By this point, they have already
        /// had their alignments and sizes calculated.
        pub fn from_fields(fields: Vec<Field>) -> Self {
            // "The alignment of the struct is the alignment of the most-aligned
            // field in it, or one if there are no fields."
            let alignment = fields
                .iter()
                .map(|field| field.alignment)
                .max()
                .unwrap_or(1);
    
            // "Start with a current offset of 0 bytes."
            let mut current_offset = 0;
    
            let mut field_offsets = vec![];
            for field in &fields {
                // "If the current offset is not a multiple of the field's
                // alignment, then add padding bytes to the current offset until it
                // is a multiple of the field's alignment."
                current_offset += Self::padding_needed_for(
                    current_offset,
                    field.alignment
                );
    
                // "The offset for the field is what the current offset is now."
                field_offsets.push(current_offset);
    
                // "Then increase the current offset by the size of the field."
                current_offset += field.size;
            }
    
            // "Finally, the size of the struct is the current offset rounded up to
            // the nearest multiple of the struct's alignment."
            let size = current_offset + Self::padding_needed_for(
                current_offset,
                alignment
            );
    
            MockLayout { fields, field_offsets, alignment, size }
        }
    }
    
    #[repr(C)]
    struct Demo {
        first: u8,
        second: u32,
        third: u64,
    }
    macro_rules! fields {
        ( $( $t:ty ),+ ) => {
            vec![
                $( Field {
                    alignment: std::mem::align_of::<$t>(),
                    size: std::mem::size_of::<$t>(),
                }),+
            ]
        }
    }
    let fields = fields![u8, u32, u64];
    let demo_layout = MockLayout::from_fields(fields);
    assert_eq!(std::mem::align_of::<Demo>(), demo_layout.alignment);
    assert_eq!(std::mem::size_of::<Demo>(), demo_layout.size);
}
