// { dg-additional-options "-w" }

trait SliceProperties {
    fn is_empty_ext(&self) -> bool;
}

impl<T> SliceProperties for [T] {
    fn is_empty_ext(&self) -> bool {
        self.is_empty()
    }
}
