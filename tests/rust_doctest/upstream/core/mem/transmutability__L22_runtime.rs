// Extracted from library/core/src/mem/transmutability.rs:22
#![allow(unused)]
fn main() {
    pub unsafe fn transmute_via_union<Src, Dst>(src: Src) -> Dst {
        use core::mem::ManuallyDrop;

        #[repr(C)]
        union Transmute<Src, Dst> {
            src: ManuallyDrop<Src>,
            dst: ManuallyDrop<Dst>,
        }

        let transmute = Transmute {
            src: ManuallyDrop::new(src),
        };

        let dst = unsafe { transmute.dst };

        ManuallyDrop::into_inner(dst)
    }
}
