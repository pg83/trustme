#![allow(dead_code)]

async fn add(x: u32, y: u32) -> u32 {
    async { x + y }.await
}

fn main() {}
