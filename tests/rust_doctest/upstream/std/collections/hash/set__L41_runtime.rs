// Extracted from library/std/src/collections/hash/set.rs:41
#![allow(unused)]
fn main() {
    use std::collections::HashSet;
    // Type inference lets us omit an explicit type signature (which
    // would be `HashSet<String>` in this example).
    let mut books = HashSet::new();
    
    // Add some books.
    books.insert("A Dance With Dragons".to_string());
    books.insert("To Kill a Mockingbird".to_string());
    books.insert("The Odyssey".to_string());
    books.insert("The Great Gatsby".to_string());
    
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
