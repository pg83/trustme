use std::ops::Add;

#[derive(Clone, Copy)]
struct Number(u32);

impl Add for Number {
    type Output = Number;
    fn add(self, other: Number) -> Number { Number(self.0 + other.0) }
}

macro_rules! forward_ref_binop {
    ($trait:ident, $method:ident for $left:ty, $right:ty) => {
        impl $trait<$right> for &$left {
            type Output = <$left as $trait<$right>>::Output;
            fn $method(self, other: $right) -> Self::Output { $trait::$method(*self, other) }
        }
        impl $trait<&$right> for $left {
            type Output = <$left as $trait<$right>>::Output;
            fn $method(self, other: &$right) -> Self::Output { $trait::$method(self, *other) }
        }
        impl $trait<&$right> for &$left {
            type Output = <$left as $trait<$right>>::Output;
            fn $method(self, other: &$right) -> Self::Output { $trait::$method(*self, *other) }
        }
    }
}

forward_ref_binop!(Add, add for Number, Number);
