use std::ops::Index;

struct Shelf(u8, u8);

impl Index<u8> for Shelf {
    type Output = u8;

    fn index(&self, _: u8) -> &u8 {
        &self.0
    }
}

impl Index<i32> for Shelf {
    type Output = u8;

    fn index(&self, _: i32) -> &u8 {
        &self.1
    }
}

fn main() {
    let selected: u8 = Shelf(17, 29)[0];
    assert_eq!(selected, 29);
}
