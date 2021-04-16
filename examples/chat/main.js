const socket = new WebSocket(`ws://${address}:${port}/chat`);
// Connection opened
socket.addEventListener('open', function (event) {{
    // socket.send('Hello Server!');
}});

function send_message() {
    socket.send($('#user-message').val())
    $('#user-message').val('')
}

$('#user-message').keydown(function (e) {
    if (e.ctrlKey && e.keyCode == 13) {
        send_message()
    }
});

$('#user-form').submit(function( event ) {
    send_message()
    event.preventDefault();
});

// Listen for messages
socket.addEventListener('message', function (event) {{
    console.log('Message from server ', event.data);
    $('#room-messages').append(`<li class="list-group-item">${event.data}</li>`)
}});
