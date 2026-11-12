struct Table;
struct Factory;

trait Family<Input> {
    type Output: Default;
}

impl Family<u8> for Table {
    type Output = u16;
}

impl Family<u16> for Table {
    type Output = u32;
}

trait Produce<Output> {
    fn produce() -> Output;
}

impl<Input> Produce<(Input, <Table as Family<Input>>::Output)> for Factory
where
    Input: Default,
    Table: Family<Input>,
{
    fn produce() -> (Input, <Table as Family<Input>>::Output) {
        (Input::default(), Default::default())
    }
}

fn main() {
    let narrow = Factory::produce();
    let (first, second): (u8, u16) = narrow;
    assert_eq!((first, second), (0, 0));

    let wide = Factory::produce();
    let (first, second): (u16, u32) = wide;
    assert_eq!((first, second), (0, 0));
}
