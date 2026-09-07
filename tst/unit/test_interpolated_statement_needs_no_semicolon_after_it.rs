/* clap 3's `tags!`: a `$action:stmt` fragment expanded ahead of `let rest = ..;` - upstream
   `parse_full_stmt` looks for no semicolon after a statement fragment. */
struct W {
    out: String,
}

impl W {
    fn good(&mut self, s: &str) -> Result<(), ()> {
        self.out.push_str(s);
        Ok(())
    }

    fn none(&mut self, s: &str) -> Result<(), ()> {
        self.out.push_str(s);
        Ok(())
    }

    fn run(&mut self, part: &str) -> Result<(), ()> {
        macro_rules! tags {
            (
                match $part:ident {
                    $( $tag:expr => $action:stmt )*
                }
            ) => {
                match $part {
                    $(
                        part if part.starts_with(concat!($tag, "}")) => {
                            $action
                            let rest = &part[$tag.len() + 1..];
                            self.none(rest)?;
                        }
                    )*
                    _ => {
                        self.none(part)?;
                    }
                }
            };
        }
        tags! {
            match part {
                "good" => self.good("<g>")?
                "bad" => self.good("<b>")?
            }
        }
        Ok(())
    }
}

fn main() {
    let mut w = W { out: String::new() };
    w.run("good}yes").unwrap();
    w.run("plain").unwrap();
    w.run("bad}no").unwrap();
    assert_eq!(w.out, "<g>yesplain<b>no");
}
