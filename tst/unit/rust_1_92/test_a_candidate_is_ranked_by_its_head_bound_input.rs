// tower-http's `trim_trailing_slash`: `if .. { Cow::Owned(format!(..)) }
// else { new_path.into() }.parse()`. rustc unifies the `into()` result with
// the `Cow` the `if` takes, and `?T: From<String>` then has one candidate.
// Our candidate selection ranks candidates by whether their input coerces
// into that `Cow`; it read `impl<T> From<T> for T` with an open `T`, an
// undecided coercion, though the impl head has `T = String`, which cannot
// become a `Cow`. With undecided coercions no longer losing, the call was
// ambiguous. A candidate's inputs are now read through its head equalities.
mod http {
    use std::str::FromStr;

    #[derive(Debug)]
    pub struct InvalidUri;

    #[derive(Clone, Debug, PartialEq)]
    pub struct PathAndQuery {
        data: String,
    }

    impl PathAndQuery {
        pub fn path(&self) -> &str {
            self.data.split('?').next().unwrap()
        }

        pub fn query(&self) -> Option<&str> {
            self.data.split_once('?').map(|(_, q)| q)
        }
    }

    impl FromStr for PathAndQuery {
        type Err = InvalidUri;
        fn from_str(s: &str) -> Result<Self, InvalidUri> {
            Ok(PathAndQuery { data: s.to_string() })
        }
    }

    pub struct Parts {
        pub path_and_query: Option<PathAndQuery>,
    }

    #[derive(Clone, Debug, PartialEq)]
    pub struct Uri {
        path_and_query: Option<PathAndQuery>,
    }

    impl Uri {
        pub fn path(&self) -> &str {
            self.path_and_query.as_ref().map_or("/", |p| p.path())
        }

        pub fn into_parts(self) -> Parts {
            Parts { path_and_query: self.path_and_query }
        }

        pub fn from_parts(parts: Parts) -> Result<Uri, InvalidUri> {
            Ok(Uri { path_and_query: parts.path_and_query })
        }

        pub fn from_static(s: &'static str) -> Uri {
            Uri { path_and_query: Some(s.parse().unwrap()) }
        }
    }
}

use http::Uri;
use std::borrow::Cow;

fn trim_trailing_slash(uri: &mut Uri) {
    if !uri.path().ends_with('/') && !uri.path().starts_with("//") {
        return;
    }

    let new_path = format!("/{}", uri.path().trim_matches('/'));

    let mut parts = uri.clone().into_parts();

    let new_path_and_query = if let Some(path_and_query) = &parts.path_and_query {
        let new_path_and_query = if let Some(query) = path_and_query.query() {
            Cow::Owned(format!("{}?{}", new_path, query))
        } else {
            new_path.into()
        }
        .parse()
        .unwrap();

        Some(new_path_and_query)
    } else {
        None
    };

    parts.path_and_query = new_path_and_query;
    if let Ok(new_uri) = Uri::from_parts(parts) {
        *uri = new_uri;
    }
}

struct Body(String);

impl From<String> for Body {
    fn from(s: String) -> Body {
        Body(s)
    }
}

fn main() {
    let mut uri = Uri::from_static("/foo/?a=1");
    trim_trailing_slash(&mut uri);
    assert_eq!(uri, Uri::from_static("/foo?a=1"));
    let mut uri = Uri::from_static("/bar/");
    trim_trailing_slash(&mut uri);
    assert_eq!(uri, Uri::from_static("/bar"));
    assert_eq!(Body::from(String::from("x")).0, "x");
}
