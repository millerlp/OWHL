# Pressure_logger_scratch2.R
# Test code to open data from the OWHL wave height pressure logger 
# prototype. 
# This version assumes that the logger was sampling continuously all day, so 
# there should be roughly 345,600 samples per day. If the sampling frequency
# was changed, or broken up into smaller chunks, this code would need to be
# modified to account for the large gaps. 

# Author: Luke Miller May 16, 2015
###############################################################################
require(marelac)
require(zoo)
## Show fractional seconds when printing to terminal
options(digit.secs = 4) 

# Time zones are a nightmare, and get used several times in this script. Enter
# the appropriate code for your data here. Ideally it's in local standard time
# like "ETC/GMT+8" (west coast), or "ETC/GMT+5" (east coast), but you might have
# collected your data with the clock set to daylight savings time 
# i.e. "PST8PDT" or "EST8EDT", or even just "GMT" (which would be the ideal).
myTimeZone = 'PST8PDT'
Sys.setenv(TZ = myTimeZone)
Sys.getenv("TZ") # Prove it changed

###************************************
# Input OWHL pressure data file(s)
wdata = '15032600.CSV'
#wdata = c('15032700.CSV','15032800.CSV','15032900.CSV','15033000.CSV')
##*************************************
# Local tide station to retrieve data from
station = 9413450 # 9413450 = Monterey, CA
##*************************************

samplefreq = 4 # The sampling rate of the OWHL, usually 4Hz (samples per second)

# dir1 = location of OWHL data file
dir1 = 'D:/Miller_projects/Electronics/OWHL/20150326_Field_test/Raw_data/'

for (i in 1:length(wdata)){
	if (i == 1){
		# Read in the first input file
		df = read.csv(paste0(dir1,wdata[i]),
				colClasses = c('numeric','character','numeric','numeric',
						'numeric'), skip = 1)
	} else {
		# If multiple input files are specified, add them on to the data frame
		df = rbind(df, read.csv(paste0(dir1,wdata[i]),
						colClasses = c('numeric','character','numeric',
								'numeric','numeric'), skip = 1))
	}
	
}

# Add the fractional seconds on to the POSIXt value.
df$POSIXt = df$POSIXt + (df$frac.seconds/100)

# Order df based on the numeric value in the POSIXt column, in case files were
# read in out of order for some reason. 
df = df[order(df$POSIXt),]

# Convert the text time stamps into a POSIXct time object in the appropriate
# timezone. Also add on the fractional seconds value
df$DateTime = as.POSIXct(df$DateTime, format = '%Y-%m-%d %H:%M:%S',
		origin='1970-1-1', tz= 'PST8PDT') + (df$frac.seconds / 100)
# Note that although the printed time stamps will be in the local time zone,
# the internal representation of the POSIXct value (a numeric value of seconds
# since the start of the epoch (origin) will be in the UTC time zone, which can
# be observed by doing as.numeric(df$DateTime[1]), and plugging the result into
# a website like http://www.unixtimestamp.com/index.php to show how that value
# is converted into the UTC time zone. Or simply compare the value in 
# df$POSIXt[1] with the result of as.numeric(df$DateTime[1]).  

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

################################
# Retrieve air pressure data from local tide station

# Generate start and end times for data retrieval based on the times in df
# Note that you have to replace any spaces in the timestamp with %20 codes
# so that the URL that is eventually generated has the correct html code for 
# a space. 
startT = strftime(df$DateTime[1],format='%Y%m%d %H:%M')
startT = sub(" ","%20",startT)
endT = strftime(df$DateTime[nrow(df)],format='%Y%m%d %H:%M')
endT = sub(" ","%20",endT)
# Assemble a query, based on the API specifications described at 
# http://tidesandcurrents.noaa.gov/api/ 
query = 'http://tidesandcurrents.noaa.gov/api/datagetter?begin_date='
query = paste0(query,startT)
query = paste0(query,"&end_date=")
query = paste0(query,endT)
query = paste0(query,"&station=")
# You entered a station ID number at the top of this file, it gets used here
query = paste0(query,station)  
# Request air pressure
query = paste0(query,"&product=air_pressure&units=metric&time_zone=")
# Careful here, specify the correct desired time zone: gmt, lst, or lst_ldt
query = paste0(query,"lst_ldt")
query = paste0(query,"&format=csv")
# Send the complete URL to the CO-OPS website, requesting a csv file in return
# that can be parsed by the read.csv function.
ap = getURL(query) # Store as a text string
# Check the integrity of the response
if (regexpr("Bad", ap) != -1 | regexpr("Wrong", ap) != -1) {
	# Finding those text strings usually means the returned info is bad
	rm(ap)
	cat('Error retrieving air pressure data\n')
} else {
	# Otherwise the data are probably good, so convert them into a data frame
	airPressure = read.csv(textConnection(ap))
	# Convert the Date.Time column to a POSIXct value, using the correct time 
	# zone
	airPressure$Date.Time = as.POSIXct(airPressure$Date.Time, tz = myTimeZone)
	# Rename the pressure column to include units
	names(airPressure)[2] = "Pressure.mbar"
	# Drop the extra columns
	airPressure = airPressure[,-(c(3,4,5))]
}



#####################################################################
# Retrieve tide height data from local tide station
# Generate start and end times for data retrieval based on the times in df
# Note that you have to replace any spaces in the timestamp with %20 codes
# so that the URL that is eventually generated has the correct html code for 
# a space. 
startT = strftime(df$DateTime[1],format='%Y%m%d %H:%M')
startT = sub(" ","%20",startT)
endT = strftime(df$DateTime[nrow(df)],format='%Y%m%d %H:%M')
endT = sub(" ","%20",endT)
# Assemble a query, based on the API specifications described at 
# http://tidesandcurrents.noaa.gov/api/ 
query = 'http://tidesandcurrents.noaa.gov/api/datagetter?begin_date='
query = paste0(query,startT)
query = paste0(query,"&end_date=")
query = paste0(query,endT)
query = paste0(query,"&station=")
# You entered a station ID number at the top of this file, it gets used here
query = paste0(query,station)  
# Request tide height, giving datum and units desired
query = paste0(query,"&product=water_level&datum=MLLW&units=metric")
# Careful here, specify the correct desired time zone: gmt, lst, or lst_ldt
query = paste0(query,"&time_zone=lst_ldt")
query = paste0(query,"&format=csv")
# Send the complete URL to the CO-OPS website, requesting a csv file in return
# that can be parsed by the read.csv function.
tl = getURL(query) # Store as a text string
# Check the integrity of the response
if (regexpr("Bad", tl) != -1 | regexpr("Wrong", tl) != -1) {
	# Finding those text strings usually means the returned info is bad
	rm(tl)
	cat('Error retrieving tide data\n')
} else {
	# Otherwise the data are probably good, so convert them into a data frame
	tideLevel = read.csv(textConnection(tl))
	# Convert the Date.Time column to a POSIXct value, using the correct time 
	# zone
	tideLevel$Date.Time = as.POSIXct(tideLevel$Date.Time, tz = myTimeZone)
	# Rename the Water.Level column to include units
	names(tideLevel)[2] = "TideHt.m"
}
########################################################################





#################
# Calculate the time step between readings. Ideally these will all
# be 0.25 seconds
diffs = diff(df$DateTime)
# Summarize the time steps
table(diffs)
# Get row indices in df where time step was greater than 0.25
skipped = which(diffs > 0.25)
# Calculate total missed time, converted to minutes
missedmins = length(skipped)/60
# Calculate total time in data set based on start and end times
totalmins = round(difftime(df[nrow(df),'DateTime'],df[1,'DateTime'], 
				units = 'mins'))
cat('Missed',missedmins,'minutes of data out of',totalmins,'total minutes\n')
cat(format((missedmins/as.numeric(totalmins))*100, digits = 2),'% missed\n')

#######################################################################
# Fill in missing time points. This assumes the data were sampled continuously
# rather than for just a portion of every hour.

# Do a check to see if there are gaps longer than 2 seconds (the diffs value for
# a single missing second of data will be 1.25). 
if (max(diffs) < 2.25){
	# Convert to a zoo object
	df.zoo = zoo(df[,-(which(colnames(df)=="DateTime"))],df$DateTime)
	# Generate a sequence of timestamps for the complete dataset
	alltimes = seq(from = df$DateTime[1], to = df$DateTime[nrow(df)], by = 0.25)
	# Merge the df.zoo with the timestamp sequence, which will insert NA's in
	# spots where the original dataset missed recording data.
	df.zoo = merge(df.zoo,zoo(,alltimes), all = TRUE)
	# Generate a vector of numeric POSIXt values (seconds since epoch origin) to 
	# fill in missing values
	allsecs = seq(from = df$POSIXt[1], to = df$POSIXt[nrow(df)], by = 0.25)
	# Write the numeric seconds values in
	df.zoo[,"POSIXt"] = allsecs
	# Create the vector of frac.seconds values to fill in gaps
	allfrac = rep(c(0,25,50,75), nrow(df.zoo) / samplefreq)
	# Write the frac.seconds values in, assuming the first value is 0 as 
	# expected
	if (df.zoo$frac.seconds[1] == 0) {df.zoo[,'frac.seconds'] = allfrac}
	# The remaining columns in df.zoo are measured data (pressure and 
	# temperature), so any gaps should just be linearly interpolated, assuming 
	# the gaps are all approximately 2 seconds in length or less.
	df.zoo = na.approx(df.zoo, maxgap = 8)
}
# At this point, the data in df.zoo should be totally continuous with no gaps

###########################################
# Generate a set of pressure readings that subtract off the extra pressure from
# the silicone tubing. The air pressure downloaded from a local weather station
# tells you the true sea level pressure, which can be compared to the readings 
# from the OWHL just before it was submerged in the ocean. Someday this should
# probably be converted into an automatic process or user-interactive process.
rawSeaLevPres = 1700  # OWHL Reading just before submergence
trueSeaLevPres = 1020 # Value obtained from local weather station
# Calculate a correction value based on the sea level air pressure from 
# the measured pressure in the OWHL prior to deployment.
correc = rawSeaLevPres - slPres
# Subtract off the correction value from all OWHL pressure readings, and 
# convert to bar. This should be the approximate true pressure, including both
# atmospheric pressure and seawater pressure, without the offset introduced by
# the silcone tubing.
df.zoo$Pressure.mbar = (df.zoo$Pressure.mbar - correc)

# At this point all of the entries in df.zoo$Pressure.mbar should be "true"
# pressure: the pressure due to nominal atmospheric pressure at sea level plus 
# the pressure due to the seawater above the submerged OWHL, without the offset
# induced by the silicone tubing on the pressure port. 

##########################################
# Calculate gauge pressure values for every time point using the local sea 
# level air pressure time series, if available. 
if (exists('airPressure')) {	
	# Find the nearest airPressure value for each OWHL reading, and subtract
	# off that atmospheric pressure to calculate the gauge pressure of the 
	# OWHL (pressure due solely to seawater, not atmosphere).
	airP.mbar = zoo(airPressure[,-1],airPressure$Date.Time)
	# Merge the two datasets. There will be large gaps in the airP.mbar column
	# because the data were collected at 6min intervals, not 4Hz. 
	df.zoo2 = merge(df.zoo,airP.mbar)
	# Use zoo's na.locf to carry forward the last good value to fill in any
	# NA values until the next good value turns up in the airP.mbar. 
	# Fills in the air pressure
	# values that were available at 6 minute intervals to match the 4Hz OWHL
	# data. 
	df.zoo2 = na.locf(df.zoo2)
	# Convert back to data frame
	df = as.data.frame(df.zoo2)
	# Stick the DateTime column back in, since zoo objects lose it. 
	df$DateTime = alltimes
	# Subtract the atmospheric pressure off of Pressure.mbar as well, so that it
	# now only has 'gauge' pressure
	df$Pressure.mbar.gauge = df$Pressure.mbar - df$airP.mbar
} else {
	# If there is not airPressure data frame, just subtract off the standard
	# air pressure at sea level (in mbar). 
	df$Pressure.mbar.gauge = df$Pressure.mbar - 1013.25
}

# At this point, the column Pressure.mbar.gauge is the approximate pressure due
# solely to seawater. The column Pressure.mbar is the approximate pressure due
# to seawater + atmospheric pressure. In both cases the pressure offset induced 
# by the silicone pressure port tubing has been subtracted off. 

########################################################################
# Use marelac package functions and UNESCO equations to estimate water depth

# The sw_depth function from the marelac package takes either the gauge pressure
# (subtracting off normal atmospheric pressure) or true pressure (including 
# normal atmospheric pressure). Gauge pressure is little p argument, true 
# pressure is capital P argument, both in units of bar (not millibar). 
# Note that this calculation is for seawater at 0°C and 35 PSU. 
df$swDepth = sw_depth(p = (df$Pressure.mbar.gauge/1000), lat = 36)

#######################################################################
# Plot some data

# Generate y axis limits for the various data types
ylims = range(c(df$Pressure.mbar,airPressure$Pressure.mbar), na.rm=TRUE)
templims = range(df$TempC, na.rm=TRUE) # Temperature axis limits
tidelims = range(tideLevel$TideHt.m, na.rm=TRUE) # Tide axis limits
depthlims = range(df$swDepth, na.rm=TRUE) # Seawater depth axis limits
# or hard-coded limits:
tidelims = c(-0.5,2)
depthlims = c(13,15.5)
# Set figure margins:
par(mar = c(5,7,1,7.5))
plot(Pressure.mbar~DateTime, data = df, type = 'n', las = 1,
		ylim = ylims,
		xlab = 'Date',
		ylab = 'Pressure, mbar')
# Insert a grey background
rect(par()$usr[1],par()$usr[3],par()$usr[2],par()$usr[4],col='grey80')
# Plot the estimated water depth
usr1 = par()$usr
par('usr' = c(usr1[1],usr1[2],depthlims[1],depthlims[2]))
lines(df$DateTime,df$swDepth, col = 'orange')
axis(side = 2, at = pretty(depthlims, n = 5), labels = pretty(depthlims, n = 5),
		las = 1, col = 'orange', col.ticks = 'orange', col.axis = 'orange', 
		line = 4)
mtext(side = 2, text = 'Depth, m', line = 5.5, col = 'orange')
par('usr' = usr1)
# Replot OWHL raw pressure values
lines(Pressure.mbar~DateTime, data = df, col = 'black')
# Plot atmospheric air pressure if it is available
if (exists('airPressure')){
	lines(airPressure$Date.Time, airPressure$Pressure.mbar, col = 'forestgreen')	
}
# Plot water temperature
usr1 = par()$usr
par('usr' = c(usr1[1],usr1[2],templims[1],templims[2]))
lines(TempC~DateTime, data = df, col = 'blue')
axis(side = 4, at = pretty(templims, n = 8), labels = pretty(templims, n = 8),
		las = 1, col = 'blue', col.ticks = 'blue', col.axis = 'blue')
mtext(side = 4, text = 'Temperature, C', line = 2.5, col = 'blue')
par('usr' = usr1)
if (exists('tideLevel')){
	# Plot the tide data as well
	usr1 = par()$usr
	par('usr' = c(usr1[1],usr1[2],tidelims[1],tidelims[2]))
	lines(tideLevel$Date.Time,tideLevel$TideHt.m, col = 'red')
	axis(side = 4, at = pretty(tidelims, n = 5), 
			labels = pretty(tidelims, n = 5),
			las = 1, col = 'red', col.ticks = 'red', col.axis = 'red', line = 4)
	mtext(side = 4, text = 'Tide Height, m', line = 6, col = 'red')
	par('usr' = usr1)
}


# Legend
legend('topleft', legend = c('OWHL Pressure','Air Pressure','Temperature',
				'Tide Height'), col = c('black','forestgreen','blue','red'),
		lty = 1)
############################################


###################################################
# Plotting for a restricted portion of the dataset, by row numbers
#windows()
#starow = which.min(abs(df$DateTime - as.POSIXct('2015-03-28 12:30')))
#endrow = which.min(abs(df$DateTime - as.POSIXct('2015-03-28 12:32')))
#
#ylims = range(df$Pressure.mbar.corr[starow:endrow])
#cext = 2
#par(mar = c(5,6,1,4))
#plot(Pressure.mbar.corr~DateTime, data = df[starow:endrow,], type = 'l', las = 1,
#		ylim = ylims,
#		xlab = '',
#		ylab = 'Pressure, mbar',
#		cex.axis = cext)
#points(Pressure.mbar.corr~DateTime, data = df[starow:endrow,], pch = 20)
#usr1 = par()$usr
#par('usr' = c(usr1[1],usr1[2],10,20))
#lines(TempC~DateTime, data = df[starow:endrow,], col = 'blue')
#axis(side = 4, at = pretty(c(10,20), n = 8), labels = pretty(c(10,20), n = 8),
#		las = 1, col.axis = 'blue', col = 'blue')
#mtext(side = 4, text = 'Temperature, C', line = 2.8, col = 'blue')

