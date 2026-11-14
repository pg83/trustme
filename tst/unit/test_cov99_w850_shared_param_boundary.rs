//@ compile-only
//@ compile-flags: -O -Zdump-mir -Zstop-after=mir

fn read(value: &u32) -> u32 {
    *value
}

pub fn shared(value: &u32) -> u32 {
    read(&*value)
}

pub async fn awaited(value: u32) -> u32 {
    async move { value }.await
}

fn main() {
    let value = 7;
    let _ = shared(&value);
    let _ = awaited(value);
}
