// Extracted from src/mod/use.md:61
mod deeply {
    pub mod nested {
        pub fn function() {
            println!("called `deeply::nested::function()`");
        }
    }
}

mod cool {
    pub use crate::deeply::nested::function;
}

fn main() {
    cool::function();
}
