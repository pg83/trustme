#![feature(core_intrinsics, never_type)]

#[derive(Copy, Clone)]
enum Empty {}
struct Unit;
struct Pair<T>(T, u8);
struct Named<T> { first: u8, value: T }
struct Recursive(&'static Recursive);
enum Value { A = 2, B = 4 }
enum Data<T> { A(T), B(u8) }
union Mixed { empty: Empty, byte: u8 }
union OnlyEmpty { empty: Empty }

const fn check_array<const N: usize>() {
    core::intrinsics::assert_inhabited::<[Empty; N]>();
}

const _: () = {
    core::intrinsics::assert_inhabited::<u8>();
    core::intrinsics::assert_inhabited::<Unit>();
    core::intrinsics::assert_inhabited::<Pair<u16>>();
    core::intrinsics::assert_inhabited::<Named<u32>>();
    core::intrinsics::assert_inhabited::<Value>();
    core::intrinsics::assert_inhabited::<Data<Empty>>();
    core::intrinsics::assert_inhabited::<Mixed>();
    core::intrinsics::assert_inhabited::<OnlyEmpty>();
    core::intrinsics::assert_inhabited::<fn()>();
    core::intrinsics::assert_inhabited::<fn(Empty) -> !>();
    core::intrinsics::assert_inhabited::<unsafe extern "C" fn(i32) -> i32>();
    core::intrinsics::assert_inhabited::<*const Empty>();
    core::intrinsics::assert_inhabited::<&Empty>();
    core::intrinsics::assert_inhabited::<&mut Empty>();
    core::intrinsics::assert_inhabited::<Recursive>();
    core::intrinsics::assert_inhabited::<[Empty; 0]>();
    core::intrinsics::assert_inhabited::<[[Empty; 1]; 0]>();
    core::intrinsics::assert_inhabited::<&[Empty]>();
    core::intrinsics::assert_inhabited::<(Unit, (), [Empty; 0])>();
    check_array::<0>();
};

fn answer() -> u8 { 42 }

fn main() {
    let callback: fn() -> u8 = answer;
    let empty: [Empty; 0] = [];
    let bytes = Mixed { byte: callback() };
    assert_eq!(empty.len(), 0);
    assert_eq!(unsafe { bytes.byte }, 42);
    assert!(matches!(Data::<Empty>::B(7), Data::B(7)));
}
