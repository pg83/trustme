/* rayon's `Chain::with_producer`: `self.a.with_producer(CallbackA { callback, a_len, b: self.b })`
   with `impl<CB, B> ProducerCallback<B::Item> for CallbackA<CB, B> where B: IndexedParallelIterator,
   CB: ProducerCallback<B::Item>` - `B::Item` is `A::Item` by the enclosing impl's where-clause. */
trait ParallelIterator: Sized {
    type Item;
    fn total(self) -> usize;
}

trait IndexedParallelIterator: ParallelIterator {
    fn len(&self) -> usize;
    fn with_producer<CB: ProducerCallback<Self::Item>>(self, callback: CB) -> CB::Output;
}

trait Producer: Sized {
    type Item;
    fn items(&self) -> usize;
}

trait ProducerCallback<T> {
    type Output;
    fn callback<P>(self, producer: P) -> Self::Output
    where
        P: Producer<Item = T>;
}

struct Chain<A, B> {
    a: A,
    b: B,
}

impl<A, B> ParallelIterator for Chain<A, B>
where
    A: IndexedParallelIterator,
    B: IndexedParallelIterator<Item = A::Item>,
{
    type Item = A::Item;
    fn total(self) -> usize {
        self.a.len() + self.b.len()
    }
}

impl<A, B> IndexedParallelIterator for Chain<A, B>
where
    A: IndexedParallelIterator,
    B: IndexedParallelIterator<Item = A::Item>,
{
    fn len(&self) -> usize {
        self.a.len() + self.b.len()
    }

    fn with_producer<CB>(self, callback: CB) -> CB::Output
    where
        CB: ProducerCallback<Self::Item>,
    {
        let a_len = self.a.len();
        return self.a.with_producer(CallbackA {
            callback,
            a_len,
            b: self.b,
        });

        struct CallbackA<CB, B> {
            callback: CB,
            a_len: usize,
            b: B,
        }

        impl<CB, B> ProducerCallback<B::Item> for CallbackA<CB, B>
        where
            B: IndexedParallelIterator,
            CB: ProducerCallback<B::Item>,
        {
            type Output = CB::Output;

            fn callback<A>(self, a_producer: A) -> Self::Output
            where
                A: Producer<Item = B::Item>,
            {
                let n = a_producer.items() + self.a_len;
                self.b.with_producer(CallbackB { callback: self.callback, a_len: n, a_producer })
            }
        }

        struct CallbackB<CB, A> {
            callback: CB,
            a_len: usize,
            a_producer: A,
        }

        impl<CB, A> ProducerCallback<A::Item> for CallbackB<CB, A>
        where
            A: Producer,
            CB: ProducerCallback<A::Item>,
        {
            type Output = CB::Output;

            fn callback<B>(self, b_producer: B) -> Self::Output
            where
                B: Producer<Item = A::Item>,
            {
                let producer = Pair { n: self.a_len + b_producer.items(), a: self.a_producer, b: b_producer };
                self.callback.callback(producer)
            }
        }

        struct Pair<A, B> {
            n: usize,
            a: A,
            b: B,
        }

        impl<A: Producer, B: Producer<Item = A::Item>> Producer for Pair<A, B> {
            type Item = A::Item;
            fn items(&self) -> usize {
                self.n
            }
        }
    }
}

struct Range(usize);

impl ParallelIterator for Range {
    type Item = usize;
    fn total(self) -> usize {
        self.0
    }
}

impl IndexedParallelIterator for Range {
    fn len(&self) -> usize {
        self.0
    }
    fn with_producer<CB: ProducerCallback<usize>>(self, callback: CB) -> CB::Output {
        callback.callback(RangeProducer(self.0))
    }
}

struct RangeProducer(usize);

impl Producer for RangeProducer {
    type Item = usize;
    fn items(&self) -> usize {
        self.0
    }
}

struct Count;

impl ProducerCallback<usize> for Count {
    type Output = usize;
    fn callback<P: Producer<Item = usize>>(self, producer: P) -> usize {
        producer.items()
    }
}

fn main() {
    let chain = Chain { a: Range(3), b: Range(4) };
    assert_eq!(chain.with_producer(Count), 3 + 3 + 4);
}
