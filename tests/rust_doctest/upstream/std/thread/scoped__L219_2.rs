// Extracted from library/std/src/thread/scoped.rs:219
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        use std::thread;
        
        let mut a = vec![1, 2, 3];
        let mut x = 0;
        
        thread::scope(|s| {
            thread::Builder::new()
                .name("first".to_string())
                .spawn_scoped(s, ||
            {
                println!("hello from the {:?} scoped thread", thread::current().name());
                // We can borrow `a` here.
                dbg!(&a);
            })
            .unwrap();
            thread::Builder::new()
                .name("second".to_string())
                .spawn_scoped(s, ||
            {
                println!("hello from the {:?} scoped thread", thread::current().name());
                // We can even mutably borrow `x` here,
                // because no other threads are using it.
                x += a[0] + a[2];
            })
            .unwrap();
            println!("hello from the main thread");
        });
        
        // After the scope, we can modify and access our variables again:
        a.push(4);
        assert_eq!(x, a.len());
        Ok(())
    }
    doctest().unwrap();
}
