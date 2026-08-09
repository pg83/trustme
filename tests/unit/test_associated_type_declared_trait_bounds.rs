trait BlockCore {
    type Item;
    type Results: AsRef<[Self::Item]> + AsMut<[Self::Item]> + Default;
}

struct Block<R: BlockCore> {
    results: R::Results,
}

impl<R: BlockCore<Item = u32>> Block<R> {
    fn update(&mut self) -> usize {
        let len = self.results.as_ref().len();
        self.results.as_mut()[0] = 7;
        len
    }
}

struct Four;

impl BlockCore for Four {
    type Item = u32;
    type Results = [u32; 4];
}

fn main() {
    let mut block = Block::<Four> { results: Default::default() };
    assert_eq!(block.update(), 4);
    assert_eq!(block.results[0], 7);
}
