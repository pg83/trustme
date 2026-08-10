// Extracted from src/mod/split.md:66
#![allow(unused)]
fn main() {
    pub fn function() {
        println!("called `my::nested::function()`");
    }
    
    #[allow(dead_code)]
    fn private_function() {
        println!("called `my::nested::private_function()`");
    }
}
