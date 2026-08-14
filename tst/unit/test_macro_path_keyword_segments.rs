macro_rules! paths {
    ($($path:path),* $(,)*) => {};
}

fn main() {
    paths!(any, super, super::super::self::path, X<Y>::Z<'static, T = U>);
}
