// Arrays of arrays of values with drop glue: `[[T; 2]; 2]`, `[[[T; 1]; 2]; 2]`
// and a `Vec<[String; 2]>`. Each level of drop glue walks its own elements.
// The C backend named every level's loop index `i`, so an inner loop shadowed
// the outer one and `x.DATA[i].DATA[i]` dropped the wrong elements: a double
// free in image's WebP decoder (`Vec<[HuffmanTree; 5]>`), a plain
// `vec![[s1, s2]]` too.
use std::cell::Cell;

struct Counted<'a>(&'a Cell<u32>, u32);

impl Drop for Counted<'_> {
    fn drop(&mut self) {
        self.0.set(self.0.get() + self.1);
    }
}

fn main() {
    let total = Cell::new(0);
    {
        let _grid = [[Counted(&total, 1), Counted(&total, 2)], [Counted(&total, 4), Counted(&total, 8)]];
    }
    assert_eq!(total.get(), 15);

    let deep = Cell::new(0);
    {
        let _cube = [[[Counted(&deep, 1)], [Counted(&deep, 2)]], [[Counted(&deep, 4)], [Counted(&deep, 8)]]];
    }
    assert_eq!(deep.get(), 15);

    let rows = vec![[String::from("a"), String::from("b")], [String::from("c"), String::from("d")]];
    assert_eq!(rows.concat(), ["a", "b", "c", "d"]);
    let copy = rows[..].to_vec();
    drop(rows);
    assert_eq!(copy[1][0], "c");
}
