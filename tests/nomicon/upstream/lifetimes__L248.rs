// Extracted from src/lifetimes.md:248
#![allow(unused)]
fn main() {
    fn example() -> Result<(), impl std::fmt::Debug> {
        #[derive(Debug)]
        struct X<'a>(&'a i32);
        
        impl Drop for X<'_> {
            fn drop(&mut self) {}
        }
        
        let mut data = vec![1, 2, 3];
        let x = X(&data[0]);
        println!("{:?}", x);
        data.push(4);
        // Here, the destructor is run and therefore this'll fail to compile.
        Ok(())
    }
    example().unwrap();
}
