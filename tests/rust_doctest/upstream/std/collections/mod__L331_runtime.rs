// Extracted from library/std/src/collections/mod.rs:331
#![allow(unused)]
fn main() {
    use std::collections::btree_map::BTreeMap;
    
    // A client of the bar. They have a blood alcohol level.
    struct Person { blood_alcohol: f32 }
    
    // All the orders made to the bar, by client ID.
    let orders = vec![1, 2, 1, 2, 3, 4, 1, 2, 2, 3, 4, 1, 1, 1];
    
    // Our clients.
    let mut blood_alcohol = BTreeMap::new();
    
    for id in orders {
        // If this is the first time we've seen this customer, initialize them
        // with no blood alcohol. Otherwise, just retrieve them.
        let person = blood_alcohol.entry(id).or_insert(Person { blood_alcohol: 0.0 });
    
        // Reduce their blood alcohol level. It takes time to order and drink a beer!
        person.blood_alcohol *= 0.9;
    
        // Check if they're sober enough to have another beer.
        if person.blood_alcohol > 0.3 {
            // Too drunk... for now.
            println!("Sorry {id}, I have to cut you off");
        } else {
            // Have another!
            person.blood_alcohol += 0.1;
        }
    }
}
