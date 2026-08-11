// { dg-additional-options "-w" }

struct Pointer<T>(*const T);

impl<T> Pointer<T> {
    fn test(self) {}
}
