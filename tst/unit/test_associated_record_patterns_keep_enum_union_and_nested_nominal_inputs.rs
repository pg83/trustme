//@ compile-variant: -O
#![feature(more_qualified_paths)]
struct Record<T> { value: T }
trait Project { type Output; }
enum EnumSource<T> { Stored(T) }
union UnionSource<T: Copy> { stored: T }
impl<T> Project for EnumSource<T> { type Output = Record<T>; }
impl<T: Copy> Project for UnionSource<T> { type Output = Record<T>; }
fn main() {
    let <EnumSource<u16> as Project>::Output { value } = Record { value: 863u16 };
    assert_eq!(value, 863);
    let <UnionSource<u16> as Project>::Output { value } = Record { value: 877u16 };
    assert_eq!(value, 877);
    let <EnumSource<UnionSource<u16>> as Project>::Output { value } = Record { value: UnionSource { stored: 881u16 } };
    assert_eq!(unsafe { value.stored }, 881);
    let <EnumSource<EnumSource<u16>> as Project>::Output { value } = Record { value: EnumSource::Stored(883u16) };
    match value { EnumSource::Stored(inner) => assert_eq!(inner, 883) }
}
