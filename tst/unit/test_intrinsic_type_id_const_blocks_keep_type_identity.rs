//@ compile-variant: -O
#![feature(core_intrinsics)]

struct First(u16);
struct Second(u16);
type Alias = First;

fn main() {
    let first = const { core::intrinsics::type_id::<First>() };
    let alias = const { core::intrinsics::type_id::<Alias>() };
    let second = const { core::intrinsics::type_id::<Second>() };
    assert_eq!(first, alias);
    assert_ne!(first, second);
    assert_eq!(first, std::any::TypeId::of::<First>());
    assert_ne!(const { core::intrinsics::type_id::<fn(&'static ())>() },
               const { core::intrinsics::type_id::<for<'a> fn(&'a ())>() });
    assert_ne!(const { core::intrinsics::type_id::<[First; 2]>() },
               const { core::intrinsics::type_id::<[First; 3]>() });
    assert_eq!(First(17).0, 17);
    assert_eq!(Second(23).0, 23);
}
