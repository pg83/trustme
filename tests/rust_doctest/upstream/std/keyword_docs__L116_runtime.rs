// Extracted from library/std/src/keyword_docs.rs:116
#![allow(unused)]
fn main() {
    fn doctest() -> Result<(), impl std::fmt::Debug> {
        let inputs = vec!["Cow", "Cat", "Dog", "Snake", "Cod"];
        
        let mut results = vec![];
        for input in inputs {
            let result = 'filter: {
                if input.len() > 3 {
                    break 'filter Err("Too long");
                };
        
                if !input.contains("C") {
                    break 'filter Err("No Cs");
                };
        
                Ok(input.to_uppercase())
            };
        
            results.push(result);
        }
        
        // [Ok("COW"), Ok("CAT"), Err("No Cs"), Err("Too long"), Ok("COD")]
        println!("{:?}", results)
        Ok(())
    }
    doctest().unwrap();
}
