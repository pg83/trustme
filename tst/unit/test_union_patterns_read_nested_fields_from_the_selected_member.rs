#[derive(Clone, Copy)]
struct Point { x: u16, y: u16 }
union Storage<T: Copy> { value: T, empty: () }
fn unpack(value: Storage<(Point, [u16; 3])>) -> (u16, u16, u16) {
    unsafe {
        let Storage { value: (Point { x, y }, [_, middle, _]) } = value;
        (x, y, middle)
    }
}
fn main() {
    let value = Storage { value: (Point { x: 13, y: 21 }, [3, 5, 8]) };
    assert_eq!(unpack(value), (13, 21, 5));
}
