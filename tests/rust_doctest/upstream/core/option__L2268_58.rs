// Extracted from library/core/src/option.rs:2268
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let s: Option<String> = Some(String::from("Hello, Rustaceans!"));
        let o: Option<usize> = Option::from(&s).map(|ss: &String| ss.len());
        
        println!("Can still print s: {s:?}");
        
        assert_eq!(o, Some(18));
        Ok(())
    }
    doctest().unwrap();
}
