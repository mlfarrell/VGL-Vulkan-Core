# Verto Studio Code Style Guide

To keep things consistent, all pull-requests to the Verto Studio Code base (including the VGL Vulkan Core) should adhere to these following C++ code style guidelines.  Sorry to be a stickler, but this codebase is where I spent a lot of my time, so I prefer it to be styled a certain way consistent to the rest of the engine :).


## Indent

Please use 2-space indents (no tabs)

## Braces, Newlines, Parens & Blocks

Please use next-line braces when declaring functions and code blocks.
Do not place any extraneous newlines between braces and code (one line to separate line groups is fine).
Do not place any spaces between function calls and parameter list parentheses.
Do not place any spaces between if/for/while/etc and parentheses.
Do place spaces after commas in parameter lists.

```C++
void func()
{
  int hello = 0;

  if(externVariable)
  {
    methodCall1();
    methodCall2(hello, 0, 1, 2);
  }
  else
  {
    cmone();
  }

  for(int i = 0; i < 150; i++)
  {
    things(i);
  }
}
```

When writing lambdas, you can use the brace on the same line.  This is one of the only places where same-line braces should be used.

```C++
auto myLambda = [] {
  return 50;
};
```

In the case of one-liner if-statements, no braces are acceptable, but not required.
Use spaces between logical operators.

```
if(lazyIfStatement && lazyIfStatement2)
  lazyFunctionCall();
```

## Method & Variable Case

Use camelCase for all variable names and method names.  Use UpperCase for class names.  Use CAPS for all constants.

```C++
class SomeClass
{
public:
  static const int CONSTANT = 0;

  void someMethod();
  void anotherTypeOfMethod();

  inline int oneLinerInlinesAreOkay() { return var; }

protected:
  int var = 0;
};
````

## Pointers & References

Keep the * next to the variable name, not the type.

```C++
void *ptr = nullptr;
const auto &ref = someVar;

void method(char *str, void *ptr, ostream &ostr);

```

## Initializer Lists

Any of the following are acceptable, but be consistent within a method/block.

```C++
int arr[] = { 0, 1, 2, 3 }; //ok

vector<int> arr =           //ok
{
  0, 1, 2, 3, 4, 5,
  6, 7, 8, 9, 10, 11
};

int arr[] = {               //fine in the case of initializer lists
  0, 1, 2, 3, 4
};
```

## Literals

Floating point literals should be of the form `0.5f` and not `.5f`.
For null pointers, use `nullptr`.  Do not use `NULL` unless you are calling standard C APIs.

