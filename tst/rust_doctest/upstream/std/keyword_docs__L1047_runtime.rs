// Extracted from library/std/src/keyword_docs.rs:1047
#![allow(unused)]
fn main() {
    let capture = "hello".to_owned();
    let block = async move {
        println!("rust says {capture} from async block");
    };
}
