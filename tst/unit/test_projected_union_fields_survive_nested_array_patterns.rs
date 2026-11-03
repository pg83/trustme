trait Shape { type Word: Copy; }
struct Small;
impl Shape for Small { type Word = u32; }
union Storage<T: Copy> { pair: [T; 2], single: T }
enum Packet<T: Copy> { Empty, Filled(Storage<T>, (T, [T; 2])) }

fn extract<S: Shape>(packet: Packet<S::Word>) -> Option<[S::Word; 4]> {
    match packet {
        Packet::Empty => None,
        Packet::Filled(storage, (head, [middle, last])) => {
            Some([unsafe { storage.pair[1] }, head, middle, last])
        }
    }
}

fn main() {
    let packet = Packet::Filled(Storage { pair: [11, 23] }, (31, [47, 59]));
    assert_eq!(extract::<Small>(packet), Some([23, 31, 47, 59]));
    assert_eq!(extract::<Small>(Packet::Empty), None);
    assert_eq!(unsafe { Storage { single: 67u32 }.single }, 67);
}
