SET NOCOUNT ON
GO
SET STATISTICS IO ON
GO
SELECT t0.[Id], t0.[Email], t0.[Nickname], t0.[Password], t0.[Balance] FROM [User] t0 WHERE t0.[Nickname] = N'bulk5000'
GO
SELECT t0.[Id], t0.[Email], t0.[Nickname], t0.[Password], t0.[Balance] FROM [User] t0 WHERE t0.[Email] = N'bulk5000@loadtest.local'
GO
SET STATISTICS IO OFF
GO
SET SHOWPLAN_TEXT ON
GO
SELECT t0.[Id], t0.[Email], t0.[Nickname], t0.[Password], t0.[Balance] FROM [User] t0 WHERE t0.[Nickname] = N'bulk5000'
GO
SELECT t0.[Id], t0.[Email], t0.[Nickname], t0.[Password], t0.[Balance] FROM [User] t0 WHERE t0.[Email] = N'bulk5000@loadtest.local'
GO
SET SHOWPLAN_TEXT OFF
GO
