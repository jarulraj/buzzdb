import sqlite3

def setup_database():
    # Connect to SQLite database (or create it if it doesn't exist)
    conn = sqlite3.connect('social_media.db')
    c = conn.cursor()

    # Drop tables if they exist to start fresh
    c.execute('DROP TABLE IF EXISTS Interactions')
    c.execute('DROP TABLE IF EXISTS Posts')
    c.execute('DROP TABLE IF EXISTS Users')

    # Create tables with the updated schema
    c.execute('''
    CREATE TABLE IF NOT EXISTS Users (
        UserID TEXT PRIMARY KEY,
        UserName TEXT,
        Location TEXT
    )
    ''')

    c.execute('''
    CREATE TABLE IF NOT EXISTS Posts (
        PostID INT PRIMARY KEY,
        UserID TEXT,
        PostContent TEXT,
        FOREIGN KEY(UserID) REFERENCES Users(UserID)
    )
    ''')

    c.execute('''
    CREATE TABLE IF NOT EXISTS Interactions (
        PostID INT,
        UserID TEXT,
        ReactionType TEXT,
        Comment TEXT,
        FOREIGN KEY(PostID) REFERENCES Posts(PostID),
        FOREIGN KEY(UserID) REFERENCES Users(UserID)
    )
    ''')

    # Insert data into Users table
    users_data = [
        (1, 'Timothee Chalamet', 'Paris'),
        (2, 'Lana Condor', 'Los Angeles'),
        (3, 'Liu Yifei', 'Beijing'),
        (4, 'Burna Boy', 'Lagos'),
        (5, 'Kriti Sanon', 'Mumbai')
    ]
    c.executemany('INSERT OR IGNORE INTO Users (UserID, UserName, Location) VALUES (?, ?, ?)', users_data)

    # Insert data into Posts table
    posts_data = [
        (1001, 1, 'Excited to start filming my new movie!'),
        (1002, 2, 'Had a great time at the beach today!'),
        (1003, 3, 'Enjoying the scenery in Beijing!'),
        (1004, 4, 'Live performance tonight in Lagos!'),
        (1005, 5, 'Loving the vibrant energy of Mumbai!')
    ]
    c.executemany('INSERT INTO Posts (PostID, UserID, PostContent) VALUES (?, ?, ?)', posts_data)

    # Insert data into Interactions table
    reactions_data = [
        (1001, 2, 'Comment', 'Love it!'),
        (1002, 3, 'Like', None),
        (1002, 4, 'Like', None),
        (1002, 5, 'Like', None),
        (1003, 4, 'Like', None),
        (1004, 5, 'Comment', 'Wish I could be there!')
    ]
    c.executemany('INSERT INTO Interactions (PostID, UserID, ReactionType, Comment) VALUES (?, ?, ?, ?)', reactions_data)

    # Commit changes and close connection
    conn.commit()
    conn.close()

def execute_queries():
    # Connect to the SQLite database
    conn = sqlite3.connect('social_media.db')
    c = conn.cursor()

    # Execute a query: Count reactions per post
    c.execute('SELECT PostID, COUNT(*) AS ReactionCount FROM Interactions GROUP BY PostID')
    print("Interactions per post:")
    for row in c.fetchall():
        print(row)

    # Execute a query: Count posts per user
    c.execute('SELECT UserID, COUNT(PostID) AS TotalPosts FROM Posts GROUP BY UserID')
    print("Posts per user:")
    for row in c.fetchall():
        print(row)


    # Execute a query: Interactions per post along with post content
    c.execute('SELECT Posts.PostContent, COUNT(Interactions.ReactionType) AS TotalInteractions FROM Posts JOIN Interactions ON Posts.PostID = Interactions.PostID GROUP BY Posts.PostID')
    print("Post content and interactions:")
    for row in c.fetchall():
        print(row)

    # Execute a query: Most popular post with users who liked it
    c.execute("""
     SELECT
        Interactions.PostID,
        COUNT(*) AS TotalLikes,
        GROUP_CONCAT(DISTINCT Users.UserID || ' - ' || Users.Username) AS UsersWhoLiked
    FROM
        Interactions
    JOIN
        Users ON Interactions.UserID = Users.UserID
    WHERE
        Interactions.ReactionType = 'Like'
    GROUP BY
        Interactions.PostID
    ORDER BY
        TotalLikes DESC
    LIMIT 1;
    """)
    print("Most popular post with users who liked it:")
    for row in c.fetchall():
        print(row)

    # Close the database connection
    conn.close()

if __name__ == '__main__':
    setup_database()
    execute_queries()
