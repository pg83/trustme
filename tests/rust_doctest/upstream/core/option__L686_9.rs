// Extracted from library/core/src/option.rs:686
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let x: Option<u32> = Some(2);
        assert_eq!(x.is_none_or(|x| x > 1), true);
        
        let x: Option<u32> = Some(0);
        assert_eq!(x.is_none_or(|x| x > 1), false);
        
        let x: Option<u32> = None;
        assert_eq!(x.is_none_or(|x| x > 1), true);
        
        let x: Option<String> = Some("ownership".to_string());
        assert_eq!(x.as_ref().is_none_or(|x| x.len() > 1), true);
        println!("still alive {:?}", x);
        Ok(())
    }
    doctest().unwrap();
}
