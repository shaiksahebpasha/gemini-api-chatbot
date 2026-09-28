# ErIs Gemini C chatbot

## Requirements

- A Google Gemini API key
- C compiler
- libcurl development package

On Debian/Ubuntu:

```bash
sudo apt update
sudo apt install build-essential libcurl4-openssl-dev
```

On Fedora:

```bash
sudo dnf install gcc libcurl-devel
```

## Build

```bash
make
```

## Configure the API

Create a key in Google AI Studio, then set it in the environment. Do not hard-code it in `eris.c` or commit it to GitHub.

```bash
export GEMINI_API_KEY='paste-your-key-here'
./eris
```

The default model is `gemini-2.5-flash`. If that model is not available for your API key, choose an available model:

```bash
export GEMINI_MODEL='gemini-2.0-flash'
./eris
```

## Commands

- `/clear` clears the local conversation history.
- `/quit` or `/exit` exits the program.

## What the program handles

- JSON escaping for quotes, backslashes, newlines, tabs, and control characters
- Bounded request and response buffers
- Network and timeout errors
- Non-2xx Gemini responses, including the API's error body
- Multi-turn history using Gemini's `user` and `model` roles
- API keys through an environment variable rather than source code

The response parser intentionally extracts the first Gemini `text` field and is sufficient for normal text responses. For a larger application, use a JSON library such as cJSON or yyjson instead of manually parsing JSON.
