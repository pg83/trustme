#![feature(const_heap, core_intrinsics)]
#![allow(internal_features)]

mod data {
    use std::intrinsics::{const_allocate, const_make_global};
    use std::mem::{align_of, size_of};

    struct Cell(&'static u8);

    static PUBLISHED: &Cell = unsafe {
        let byte = const_allocate(1, 1);
        byte.write(57);
        let cell = const_allocate(size_of::<Cell>(), align_of::<Cell>()).cast::<Cell>();
        cell.write(Cell(&*const_make_global(byte)));
        &*const_make_global(cell.cast()).cast::<Cell>()
    };

    pub fn read() -> u8 {
        *PUBLISHED.0
    }
}

struct Packet;

trait ReadPacket {
    const MASK: u8;
    fn read(&self) -> u8;
}

macro_rules! read_method {
    () => { fn read(&self) -> u8 { data::read() } };
}

macro_rules! readers {
    ($member:item) => {
        impl ReadPacket for Packet {
            const MASK: u8 = 3;
            #[cfg(all())]
            $member
            #[cfg(any())]
            $member
        }

        impl Packet {
            const MASK: u8 = 7;
            #[cfg(all())]
            $member
        }
    };
}

readers!(read_method!(););

fn main() {
    assert_eq!((<Packet as ReadPacket>::read(&Packet), Packet.read()), (57, 57));
    assert_eq!((<Packet as ReadPacket>::MASK, Packet::MASK), (3, 7));
}
