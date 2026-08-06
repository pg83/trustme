// Extracted from library/std/src/keyword_docs.rs:624
#![allow(unused)]
fn main() {
    let rude = true;
    if 1 == 2 {
        println!("whoops, mathematics broke");
    } else {
        println!("everything's fine!");
    }
    
    let greeting = if rude {
        "sup nerd."
    } else {
        "hello, friend!"
    };
    
    if let Ok(x) = "123".parse::<i32>() {
        println!("{} double that and you get {}!", greeting, x * 2);
    }
}
