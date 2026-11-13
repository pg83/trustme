trait Value {
    type Ty;
    fn read(self) -> Self::Ty;
}

struct Word(u16);

impl Value for Word {
    type Ty = u16;
    fn read(self) -> u16 { self.0 }
}

struct Layer<T>(T);

impl<T: Value> Value for Layer<T> {
    type Ty = T::Ty;
    fn read(self) -> Self::Ty { self.0.read() }
}

struct Pair<T>(T);

trait Select<X> {
    fn select(self) -> X;
}

impl<T: Value> Select<<Layer<T> as Value>::Ty> for Pair<T>
where
    T::Ty: Clone,
{
    fn select(self) -> <Layer<T> as Value>::Ty {
        Layer(self.0).read().clone()
    }
}

fn evaluate<S: Select<u16>>(source: S) -> u16 {
    source.select()
}

fn main() {
    assert_eq!(evaluate(Pair(Word(251))), 251);
}
