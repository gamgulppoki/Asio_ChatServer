IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'Users')
BEGIN
    CREATE TABLE Users (
        Id INT IDENTITY(1,1) PRIMARY KEY,
        Name NVARCHAR(50),
        Email NVARCHAR(100),
        PasswordHash NVARCHAR(128)
    );
END