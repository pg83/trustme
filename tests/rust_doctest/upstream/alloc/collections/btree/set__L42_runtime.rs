// Extracted from library/alloc/src/collections/btree/set.rs:42
#![allow(unused)]
extern crate alloc;
fn main() {
    use std::collections::BTreeSet;
    
    // Type inference lets us omit an explicit type signature (which
    // would be `BTreeSet<&str>` in this example).
    let mut books = BTreeSet::new();
    
    // Add some books.
    books.insert("A Dance With Dragons");
    books.insert("To Kill a Mockingbird");
    books.insert("The Odyssey");
    books.insert("The Great Gatsby");
    
    // Check for a specific one.
    if !books.contains("The Winds of Winter") {
        println!("We have {} books, but The Winds of Winter ain't one.",
                 books.len());
    }
    
    // Remove a book.
    books.remove("The Odyssey");
    
    // Iterate over everything.
    for book in &books {
        println!("{book}");
    }
}
