#ifndef FIREBASE_CONFIG_PAGE_H
#define FIREBASE_CONFIG_PAGE_H

const char FIREBASE_CONFIG_PAGE[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>Skyboard Firebase Configuration</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
            background-color: #f0f0f0;
        }
        .container {
            max-width: 600px;
            margin: 0 auto;
            background-color: white;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            text-align: center;
            margin-bottom: 30px;
        }
        .form-group {
            margin-bottom: 20px;
        }
        label {
            display: block;
            margin-bottom: 5px;
            color: #555;
            font-weight: bold;
        }
        input[type="text"],
        input[type="password"] {
            width: 100%;
            padding: 8px;
            border: 1px solid #ddd;
            border-radius: 4px;
            box-sizing: border-box;
        }
        button {
            background-color: #4CAF50;
            color: white;
            padding: 10px 20px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            width: 100%;
            font-size: 16px;
        }
        button:hover {
            background-color: #45a049;
        }
        .info {
            background-color: #e8f5e9;
            padding: 10px;
            border-radius: 4px;
            margin-bottom: 20px;
            color: #2e7d32;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Firebase Configuration</h1>
        <div class="info">
            Please enter your Firebase credentials. The API key is pre-configured and cannot be changed.
        </div>
        <form action="/save-firebase" method="post">
            <div class="form-group">
                <label for="email">Email:</label>
                <input type="text" id="email" name="email" value="%EMAIL%" required>
            </div>
            <div class="form-group">
                <label for="password">Password:</label>
                <input type="password" id="password" name="password" value="%PASSWORD%" required>
            </div>
            <button type="submit">Save Configuration</button>
        </form>
    </div>
</body>
</html>
)rawliteral";

#endif // FIREBASE_CONFIG_PAGE_H 