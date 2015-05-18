# Pressure_logger_scratch.R
# Test code to open data from the Arduino wave height pressure logger 
# prototype. 
# Author: Luke Miller Apr 24, 2014
###############################################################################
## Show fractional seconds when printing to terminal
options(digit.secs = 4) 
#dir1 = 'D:/temp/logger_files/'
dir1 = 'D:/Miller_projects/Electronics/OWHL/20150326_Field_test/Raw_data/'
#files = dir(dir1, pattern = '*.CSV',include.dirs = FALSE, 
#		recursive = FALSE)

df = read.csv(paste0(dir1,'15032600.CSV'),
		colClasses = c('numeric','POSIXct','integer','numeric','numeric'),
		skip = 1)
# Add on the fractional seconds value
df$DateTime = df$DateTime + (df[,3]/100)

df$DateTime2 = as.POSIXct(df$POSIXt, origin = '1970-01-01') + (df[,3]/100)
df$DateTime2 = c(df$DateTime2)
## Calculate Temperature in Fahrenheit
df$TempF = df$TempC * (9/5) + 32

###########
# Filter suspect time points. Suspect time points have ms values that aren't
# equal to 0, 25, 50, or 75. 
# Make a matrix to hold the test results
filt = matrix(0,nrow = nrow(df), ncol = 5)
filt[,1] = rep(0L,nrow(df)) == df[,3] # compare every ms value to 0
filt[,2] = rep(25L,nrow(df)) == df[,3] # compare every ms value to 25
filt[,3] = rep(50L,nrow(df)) == df[,3] # compare every ms value to 50
filt[,4] = rep(75L,nrow(df)) == df[,3] # compare every ms value to 75
# Sum every row of filt. If none of the tests above returned true, the result
# in the 5th column will be 0 (== FALSE). Otherwise it will be TRUE
filt[,5] = rowSums(filt[,1:4]) 
# Now find every row that has a suspect ms value
badrows = which(!(filt[,5]))
if (length(badrows)>0){
	# Remove any rows with suspect ms values
	df = df[-badrows,]	
	cat('Removed',length(badrows),'suspect rows from data.\n')
}


###############
ylims = c(850,2300)
ylims = range(df$Pressure.mbar)
templims = c(40,90)
par(mar = c(5,4,1,4))
plot(Pressure.mbar~DateTime, data = df, type = 'l', las = 1,
		ylim = ylims,
		xlab = 'Date',
		ylab = 'Pressure, mbar')
usr1 = par()$usr
par('usr' = c(usr1[1],usr1[2],templims[1],templims[2]))
lines(TempF~DateTime, data = df, col = 'blue')
#axis(side = 4, at = pretty(df$TempF), labels = pretty(df$TempF), las = 1)
axis(side = 4, at = pretty(templims, n = 8), labels = pretty(templims, n = 8),
		las = 1, col = 'blue', col.ticks = 'blue', col.axis = 'blue')
mtext(side = 4, text = 'Temperature, F', line = 3, col = 'blue')
par('usr' = usr1)
#################
# Calculate the average time step between readings. Ideally these will all
# be 0.25 seconds
diffs = diff(df$DateTime)
# Summarize the time steps
table(diffs)
# Get row indexes in df where time step was greater than 0.25
skipped = which(diffs > 0.25)
#skipped = which(diffs > 0.50)
# Calculate total missed time, converted to minutes
missedmins = length(skipped)/60
# Calculate total time in data set based on start and end times
totalmins = round(difftime(df[nrow(df),2],df[1,2], units = 'mins'))
cat('Missed',missedmins,'minutes of data out of',totalmins,'total minutes\n')
cat(format((missedmins/as.numeric(totalmins))*100, digits = 2),'% missed\n')

# Calculate number of rows between skipped seconds. This can show if the time
# between missed time points changed over the course of the data set.
diffskip = diff(skipped)
table(diffskip)
# Calculate seconds between skipped intervals
diffskip2 = diffskip / 4
table(diffskip2)
###################################################
# Plotting for a restricted portion of the dataset, by row numbers
windows()
starow = which.min(abs(df$DateTime - as.POSIXct('2015-03-26 12:30')))
endrow = which.min(abs(df$DateTime - as.POSIXct('2015-03-26 12:32')))
ylims = c(1050,3750)
ylims = range(df$Pressure.mbar[starow:endrow])
cext = 2
par(mar = c(5,4,1,4))
plot(Pressure.mbar~DateTime, data = df[starow:endrow,], type = 'l', las = 1,
		ylim = ylims,
		xlab = '',
		ylab = 'Pressure, mbar',
		cex.axis = cext)
points(Pressure.mbar~DateTime, data = df[starow:endrow,], pch = 20)
usr1 = par()$usr
par('usr' = c(usr1[1],usr1[2],45,90))
lines(TempF~DateTime, data = df[starow:endrow,], col = 'blue')
#axis(side = 4, at = pretty(df$TempF), labels = pretty(df$TempF), las = 1)
axis(side = 4, at = pretty(c(32,110), n = 8), labels = pretty(c(32,110), n = 8),
		las = 1, col.axis = 'blue', col = 'blue')
mtext(side = 4, text = 'Temperature, F', line = 2.8, col = 'blue')

#########################################################
# Plotting for a restricted portion of the dataset, by row numbers
#png(file = "wave_trace_OWHL.png", width = 800, height = 600)
#starow = which.min(abs(df$DateTime - as.POSIXct('2015-03-26 12:30')))
#endrow = which.min(abs(df$DateTime - as.POSIXct('2015-03-26 12:34')))
#
#ylims = range(df$Pressure.mbar[starow:endrow])
#cext = 2
#par(mar = c(5,7,1,4))
#plot(Pressure.mbar~DateTime, data = df[starow:endrow,], type = 'l', las = 1,
#		ylim = ylims,
#		xlab = '',
#		ylab = '',
#		cex.axis = cext,
#		xaxt = 'n')
#points(Pressure.mbar~DateTime, data = df[starow:endrow,], pch = 20)
#mtext(side = 2, text = 'Pressure, mbar', line = 5.2, cex = cext)
#axis(side = 1, at = pretty(df$DateTime[starow:endrow]),
#				labels = format(pretty(df$DateTime[starow:endrow]),"%H:%M"),
#				cex.axis = cext)
#mtext(side = 1, text = "Time", line = 3.5, cex = cext)		
#dev.off()
#usr1 = par()$usr
#par('usr' = c(usr1[1],usr1[2],50,90))
#lines(TempF~DateTime, data = df[starow:endrow,], col = 'blue')
##axis(side = 4, at = pretty(df$TempF), labels = pretty(df$TempF), las = 1)
#axis(side = 4, at = pretty(c(32,110), n = 8), labels = pretty(c(32,110), n = 8),
#		las = 1, col.axis = 'blue', col = 'blue')
#mtext(side = 4, text = 'Temperature, F', line = 2.8, col = 'blue')