#![feature(type_ascription, builtin_syntax)]
#[inline]
pub fn identity<T>(value: T) -> T { builtin # type_ascribe(value, T) }
#[inline]
pub fn read<T: Copy>(value: &T) -> T { *builtin # type_ascribe(value, &T) }
