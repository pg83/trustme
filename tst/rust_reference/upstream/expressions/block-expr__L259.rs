// Extracted from src/expressions/block-expr.md:259
#![allow(unused)]
fn main() {
    fn foo<T>() -> usize {
        {
            struct Const<T>(T);
            impl<T> Const<T> {
                const CONST: usize = std::mem::size_of::<T>() + 1;
            }
            Const::<T>::CONST
        }
    }
}
