trait Source {
    type Item;
}

struct SourceImpl;

impl Source for SourceImpl {
    type Item = u32;
}

trait Consumer<T> {
    fn consume(self) -> u32;
}

struct Find;

impl Consumer<u32> for Find {
    fn consume(self) -> u32 {
        7
    }
}

struct Map<C, F>(C, F);

impl<T, R, C, F> Consumer<T> for Map<C, F>
where
    C: Consumer<R>,
    F: Fn(T) -> R,
    R: Send,
{
    fn consume(self) -> u32 {
        self.0.consume()
    }
}

struct FlatMap<C, F>(C, F);

impl<T, U, C, F> Consumer<T> for FlatMap<C, F>
where
    C: Consumer<<U as Source>::Item>,
    F: Fn(T) -> U,
    U: Source,
{
    fn consume(self) -> u32 {
        self.0.consume()
    }
}

fn to_u64(value: u32) -> u64 {
    u64::from(value)
}

fn to_source(_: u64) -> SourceImpl {
    SourceImpl
}

fn main() {
    let consumer = Map(
        FlatMap(Find, to_source as fn(u64) -> SourceImpl),
        to_u64 as fn(u32) -> u64,
    );
    assert_eq!(Consumer::<u32>::consume(consumer), 7);
}
