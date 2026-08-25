//@ run-pass

// A diverging closure result must keep waiting for its pending FnOnce and
// associated-type constraints. Defaulting it to `()` early breaks the
// invariant CollectResult expected by the surrounding HRTB callback.
use std::marker::PhantomData;

fn join<A, B, RA, RB>(left: A, right: B) -> (RA, RB)
where
    A: FnOnce() -> RA + Send,
    B: FnOnce() -> RB + Send,
    RA: Send,
    RB: Send,
{
    (left(), right())
}

trait Consumer<Item>: Sized {
    type Folder: Folder<Item, Result = Self::Result>;
    type Reducer: Reducer<Self::Result>;
    type Result: Send;

    fn split_at(self) -> (Self, Self, Self::Reducer);
    fn into_folder(self) -> Self::Folder;
}

trait Folder<Item>: Sized {
    type Result;

    fn consume(self, item: Item) -> Self;
    fn complete(self) -> Self::Result;
}

trait Reducer<Result> {
    fn reduce(self, left: Result, right: Result) -> Result;
}

trait UnindexedConsumer<Item>: Consumer<Item> {
    fn to_reducer(&self) -> Self::Reducer;
}

struct CollectConsumer<'c, T>(PhantomData<&'c mut T>);
struct CollectResult<'c, T>(PhantomData<&'c mut &'c mut [T]>);
struct CollectReducer;

unsafe impl<'c, T: Send> Send for CollectConsumer<'c, T> {}
unsafe impl<'c, T: Send> Send for CollectResult<'c, T> {}

impl<'c, T: Send + 'c> Consumer<T> for CollectConsumer<'c, T> {
    type Folder = CollectResult<'c, T>;
    type Reducer = CollectReducer;
    type Result = CollectResult<'c, T>;

    fn split_at(self) -> (Self, Self, Self::Reducer) {
        loop {}
    }

    fn into_folder(self) -> Self::Folder {
        loop {}
    }
}

impl<'c, T: Send + 'c> UnindexedConsumer<T> for CollectConsumer<'c, T> {
    fn to_reducer(&self) -> Self::Reducer {
        CollectReducer
    }
}

impl<'c, T> Folder<T> for CollectResult<'c, T> {
    type Result = Self;

    fn consume(self, _item: T) -> Self {
        self
    }

    fn complete(self) -> Self::Result {
        self
    }
}

impl<'c, T> Reducer<CollectResult<'c, T>> for CollectReducer {
    fn reduce(
        self,
        left: CollectResult<'c, T>,
        _right: CollectResult<'c, T>,
    ) -> CollectResult<'c, T> {
        left
    }
}

fn collect_with_consumer<T, F>(scope_fn: F)
where
    T: Send,
    F: FnOnce(CollectConsumer<'_, T>) -> CollectResult<'_, T>,
{
    let _ = scope_fn;
}

fn check() {
    collect_with_consumer(|consumer| {
        let reducer = consumer.to_reducer();
        let (left_consumer, right_consumer, _) = consumer.split_at();
        let (left_result, right_result) = join(
            || {
                let left_folder = left_consumer.into_folder();
                let _left_folder = left_folder.consume(0);
                panic!("left consumer panic");
            },
            || {
                let right_folder = right_consumer.into_folder();
                let right_folder = right_folder.consume(2);
                right_folder.complete()
            },
        );
        reducer.reduce(left_result, right_result)
    });
}

fn main() {
    check();
}
