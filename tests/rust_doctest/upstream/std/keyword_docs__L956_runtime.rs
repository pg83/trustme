// Extracted from library/std/src/keyword_docs.rs:956
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        enum Outer {
            Double(Option<u8>, Option<String>),
            Single(Option<u8>),
            Empty
        }
        
        let get_inner = Outer::Double(None, Some(String::new()));
        match get_inner {
            Outer::Double(None, Some(st)) => println!("{st}"),
            Outer::Single(opt) => println!("{opt:?}"),
            _ => panic!(),
        }
        Ok(())
    }
    doctest().unwrap();
}
