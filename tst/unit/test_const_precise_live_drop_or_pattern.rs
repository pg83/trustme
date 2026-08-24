#![feature(const_precise_live_drops)]

const _: Vec<i32> = {
    let value: Result<_, Vec<i32>> = Ok(Vec::new());
    match value {
        Ok(value) | Err(value) => value,
    }
};

fn main() {}
