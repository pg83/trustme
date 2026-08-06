// Extracted from library/core/src/alloc/layout.rs:408
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::alloc::{Layout, LayoutError};
        pub fn repr_c(fields: &[Layout]) -> Result<(Layout, Vec<usize>), LayoutError> {
            let mut offsets = Vec::new();
            let mut layout = Layout::from_size_align(0, 1)?;
            for &field in fields {
                let (new_layout, offset) = layout.extend(field)?;
                layout = new_layout;
                offsets.push(offset);
            }
            // Remember to finalize with `pad_to_align`!
            Ok((layout.pad_to_align(), offsets))
        }
        // test that it works
        #[repr(C)] struct S { a: u64, b: u32, c: u16, d: u32 }
        let s = Layout::new::<S>();
        let u16 = Layout::new::<u16>();
        let u32 = Layout::new::<u32>();
        let u64 = Layout::new::<u64>();
        assert_eq!(repr_c(&[u64, u32, u16, u32]), Ok((s, vec![0, 8, 12, 16])));
        Ok(())
    }
    doctest().unwrap();
}
