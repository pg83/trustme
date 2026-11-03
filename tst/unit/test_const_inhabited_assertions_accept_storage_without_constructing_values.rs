#![feature(core_intrinsics, never_type)]

#[derive(Copy, Clone)]
enum Void {}
struct Label;
struct Prefix<T>(u8, T);
struct Record<T> { tag: Label, contents: T }
struct Link { next: &'static Link }
enum Tag { First = 5, Last = 9 }
enum Choice<T> { Missing(T), Present(Prefix<u16>) }
union Storage { absent: Void }

const CHECK: () = {
    core::intrinsics::assert_inhabited::<bool>();
    core::intrinsics::assert_inhabited::<Label>();
    core::intrinsics::assert_inhabited::<Prefix<u16>>();
    core::intrinsics::assert_inhabited::<Record<[Void; 0]>>();
    core::intrinsics::assert_inhabited::<Tag>();
    core::intrinsics::assert_inhabited::<Choice<Void>>();
    core::intrinsics::assert_inhabited::<Storage>();
    core::intrinsics::assert_inhabited::<&mut Void>();
    core::intrinsics::assert_inhabited::<*const Void>();
    core::intrinsics::assert_inhabited::<Link>();
    core::intrinsics::assert_inhabited::<fn(Void) -> !>();
    core::intrinsics::assert_inhabited::<[u16; 3]>();
    core::intrinsics::assert_inhabited::<[[Void; 4]; 0]>();
    core::intrinsics::assert_inhabited::<(Label, Record<u8>, ())>();
};

fn main() {
    let value = Choice::<Void>::Present(Prefix(2, 40));
    let answer = match value {
        Choice::Present(Prefix(prefix, contents)) => u16::from(prefix) + contents,
        Choice::Missing(absent) => match absent {},
    };
    assert_eq!(answer, 42);
    assert_eq!(CHECK, ());
}
