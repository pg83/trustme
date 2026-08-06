// Extracted from library/core/src/ops/arith.rs:972
#![allow(unused)]
fn main() {
    use std::ops::RemAssign;
    
    struct CookieJar { cookies: u32 }
    
    impl RemAssign<u32> for CookieJar {
        fn rem_assign(&mut self, piles: u32) {
            self.cookies %= piles;
        }
    }
    
    let mut jar = CookieJar { cookies: 31 };
    let piles = 4;
    
    println!("Splitting up {} cookies into {} even piles!", jar.cookies, piles);
    
    jar %= piles;
    
    println!("{} cookies remain in the cookie jar!", jar.cookies);
}
