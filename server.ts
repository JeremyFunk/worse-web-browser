const server = Bun.serve({
    port: 3000,
    fetch(request) {
      console.log("TEST")
      return new Response("Welcome to Bun!");
    },
  });
  
  console.log(`Listening on ${server.url}`);