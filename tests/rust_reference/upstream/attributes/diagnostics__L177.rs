// Extracted from src/attributes/diagnostics.md:177
#![allow(unused)]
fn main() {
    #[expect(unused_variables)]
    fn select_song() {
        // This will emit the `unused_variables` lint at the warn level
        // as defined by the `warn` attribute. This will not fulfill the
        // expectation above the function.
        #[warn(unused_variables)]
        let song_name = "Crab Rave";
    
        // The `allow` attribute suppresses the lint emission. This will not
        // fulfill the expectation as it has been suppressed by the `allow`
        // attribute and not the `expect` attribute above the function.
        #[allow(unused_variables)]
        let song_creator = "Noisestorm";
    
        // This `expect` attribute will suppress the `unused_variables` lint emission
        // at the variable. The `expect` attribute above the function will still not
        // be fulfilled, since this lint emission has been suppressed by the local
        // expect attribute.
        #[expect(unused_variables)]
        let song_version = "Monstercat Release";
    }
}
