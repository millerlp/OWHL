# Pressure_logger_scratch_longterm.R
# Test code to open data from the Arduino wave height pressure logger 
# prototype. This version pastes all of the data files in a directory into 
# one long data frame, sorted by time stamp. If you have lots of input files
# it can take several minutes to load them all, and to plot them all. 
# Author: Luke Miller Sep 16, 2014
###############################################################################
## Show fractional seconds when printing to terminal
options(digit.secs = 4) 
dir1 = 'D:/temp/logger_files/freezer_trial_201408/'
files = dir(dir1, pattern = '*.CSV',include.dirs = FALSE, 
		recursive = FALSE)
# Open all of the files and combine the data into one data frame 'df'
for (fnum in 1:length(files)){
	if (fnum == 1){
		df = read.csv(paste0(dir1,files[1]),
				colClasses = c('numeric','POSIXct','integer','numeric',
						'numeric'))
	} else {
		# Open each successive file and append it to the data frame df
		dftemp = read.csv(paste0(dir1,files[fnum]),
				colClasses = c('numeric','POSIXct','integer','numeric',
						'numeric'))
		df = rbind(df,dftemp)
		cat('Opening file ', fnum, '\n'); flush.console();
	}
}


# Add on the fractional seconds value
df$DateTime = df$DateTime + (df[,3]/100)

df$DateTime2 = as.POSIXct(df$POSIXt, origin = '1970-01-01') + (df[,3]/100)
df$DateTime2 = c(df$DateTime2)
## Calculate Temperature in Fahrenheit
df$TempF = df$TempC * (9/5) + 32

# Reorder based on the values in DateTime
df = df[order(df$DateTime),]

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
# If you've pasted a lot of files together this could take a minute or two to 
# actually produce a file.
png("pressure_dataset.png", width = 1024, height = 480)
ylims = c(850,2300)
ylims = range(df$Pressure.mbar)
ylims2 = range(df$TempF, na.rm = TRUE)
par(mar = c(5,4,1,4))
plot(Pressure.mbar~DateTime, data = df, type = 'l', las = 1,
		ylim = ylims,
		xlab = 'Date',
		ylab = 'Pressure, mbar')
usr1 = par()$usr
par('usr' = c(usr1[1],usr1[2],ylims2[1],ylims2[2]))
lines(TempF~DateTime, data = df, col = 'blue')
#axis(side = 4, at = pretty(df$TempF), labels = pretty(df$TempF), las = 1)
axis(side = 4, at = pretty(ylims2, n = 8), labels = pretty(ylims2, n = 8),
		las = 1, col = 'blue', col.ticks = 'blue', col.axis = 'blue')
mtext(side = 4, text = 'Temperature, F', line = 3, col = 'blue')
dev.off()
#################
# Calculate the average time step between readings. Ideally these will all
# be 0.25 seconds
diffs = diff(df$DateTime)
# Summarize the time steps
table(diffs)
# Get row indexes in df where time step was greater than 0.25
skipped = which(diffs > 0.25)
skipped = which(diffs > 0.50)
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
#windows()
#starow = which.min(abs(df$DateTime - as.POSIXct('2014-08-24 17:00')))
#endrow = which.min(abs(df$DateTime - as.POSIXct('2014-08-24 20:00')))
#ylims = c(1050,2200)
#ylims = range(df$Pressure.mbar[starow:endrow])
#par(mar = c(5,4,1,4))
#plot(Pressure.mbar~DateTime, data = df[starow:endrow,], type = 'l', las = 1,
#		ylim = ylims,
#		xlab = 'Date',
#		ylab = 'Pressure, mbar')
#points(Pressure.mbar~DateTime, data = df[starow:endrow,], pch = 20)
#usr1 = par()$usr
#par('usr' = c(usr1[1],usr1[2],50,90))
#lines(TempF~DateTime, data = df[starow:endrow,], col = 'blue')
##axis(side = 4, at = pretty(df$TempF), labels = pretty(df$TempF), las = 1)
#axis(side = 4, at = pretty(c(32,110), n = 8), labels = pretty(c(32,110), n = 8),
#		las = 1, col.axis = 'blue', col = 'blue')
#mtext(side = 4, text = 'Temperature, F', line = 2.8, col = 'blue')

