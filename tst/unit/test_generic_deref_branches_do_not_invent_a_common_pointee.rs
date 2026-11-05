use std::marker::PhantomData;
use std::ops::Deref;

struct Left<T>(PhantomData<T>);
struct Right<T>(PhantomData<T>);

static RIGHT_END: Right<u32> = Right(PhantomData);
static LEFT_END: Left<u64> = Left(PhantomData);

impl Deref for Left<u8> {
    type Target = Right<u32>;

    fn deref(&self) -> &Self::Target {
        &RIGHT_END
    }
}

impl Deref for Right<u16> {
    type Target = Left<u64>;

    fn deref(&self) -> &Self::Target {
        &LEFT_END
    }
}

fn main() {
    let left = Left(PhantomData);
    let right = Right(PhantomData);
    let flag = std::hint::black_box(true);
    let _joined = if flag { &left } else { &right };
}
