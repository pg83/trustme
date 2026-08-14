#![feature(generic_const_exprs)]
#![allow(incomplete_features)]

fn concat<const A: usize, const B: usize>(left: [f32; A], right: [f32; B]) -> [f32; A + B] {
    let _ = (left, right);
    [0.0; A + B]
}

async fn identity<const N: usize>(value: [f32; N]) -> [f32; N] {
    value
}

async fn diverging<const N: usize>() -> [f32; N] {
    loop {}
}

async fn use_output() {
    let joined = concat([0.0], [1.0, 2.0]);
    let _: [f32; 3] = identity(joined).await;
}

fn main() {
    let _ = use_output();
    let _ = diverging::<3>();
}
