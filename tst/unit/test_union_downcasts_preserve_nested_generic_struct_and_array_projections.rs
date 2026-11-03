//@ compile-variant: -O
#[repr(C)]
union Payload<T: Copy> { array: [T; 4], scalar: T }
struct Frame<T: Copy> { payload: Payload<T>, extra: T }
struct Pair<T>(T, T);
enum Packet<T: Copy> { Data(Frame<T>), Empty }

fn update<T: Copy>(packet: &mut Packet<T>, index: usize, next: T) -> T {
    match packet {
        Packet::Data(frame) => unsafe {
            let previous = frame.payload.array[index];
            frame.payload.array[index] = next;
            previous
        },
        Packet::Empty => next,
    }
}

fn main() {
    let mut packet = Packet::Data(Frame { payload: Payload { array: [11u16, 13, 17, 19] }, extra: 23 });
    assert_eq!(update(&mut packet, std::hint::black_box(2), 29), 17);
    if let Packet::Data(frame) = packet {
        let Pair(first, second) = Pair(unsafe { frame.payload.array[2] }, frame.extra);
        assert_eq!((first, second), (29, 23));
        assert_eq!(unsafe { frame.payload.scalar }, 11);
    }
    assert_eq!(update(&mut Packet::Empty, 0, 31u32), 31);
}
