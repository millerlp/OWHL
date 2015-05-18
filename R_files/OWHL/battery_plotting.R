# battery_plotting.R
# 
# Author: Luke Miller Aug 15, 2014
###############################################################################

# This version of the script is for battery usage in the run started on 
# 2014-11-05, using 3 OWHLs in the lab freezer, all powered off 3 D-cell 
# batteries. The three OWHLs were programmed with OWHL.ino commit 
# tagged edf4f411bcb3d4acbf42c3b95052d8fa68caa473 on Github. 

require(xlsx)
dir1 = "D:/Miller_projects/Electronics/OWHL/battery_test/"
df = read.xlsx(paste0(dir1,"OWHL_battery_test_20141105.xlsx"), sheetIndex=1)
# Calculate elapsed time since start of test, in days
df$elapsed = difftime(df[,1],df[1,1], units = 'days')

# Open original battery test from August 2014. This Revision A unit was 
# programmed to log continuously, and was programmed with the OWHL.ino 
# commit tagged c6b6d381f1a246f6bd7633805832e801c4c93f3f on GitHub. 
df2 = read.csv(paste0(dir1,'RevA_Freezer_battery_test_20140815.csv'))
# Convert the excel-formatted date and time to POSIXct
df2$DateTime = as.POSIXct(df2$DateTime, format='%m/%d/%Y %H:%M')
df2$elapsed = difftime(df2[,1],df2[1,1], units = 'days')

################################################################################
# Make some estimate of battery run time based on straight-line fits
# to portions of the voltage curves.

lowerlim = 3.1 # assumed dropout voltage for 3.0v regulator
# fit linear model to voltage data
mod = lm(RevA.3030.high~elapsed, data = df)
# use model coefficients to estimate time to reach lowerlim voltage
estdays = (lowerlim - mod$coefficients[1]) / mod$coefficients[2]
cat('Estimated days to dropout voltage:\n')
cat('RevA.3030.high: ',estdays,'\n')

# fit linear model to voltage data
mod = lm(RevB.60.Internal~elapsed, data = df)
# use model coefficients to estimate time to reach lowerlim voltage
estdays = (lowerlim - mod$coefficients[1]) / mod$coefficients[2]
cat('RevB.60.Internal: ',estdays,'\n')

# fit linear model to voltage data
mod = lm(RevB.60.External~elapsed, data = df)
# use model coefficients to estimate time to reach lowerlim voltage
estdays = (lowerlim - mod$coefficients[1]) / mod$coefficients[2]
cat('RevB.60.External: ',estdays,'\n')

######################################
dayoffset = 10
# fit linear model to voltage data
modA = lm(RevA.3030.high~elapsed, data = df[df$elapsed > dayoffset, ])
# use model coefficients to estimate time to reach lowerlim voltage
estdays = (lowerlim - modA$coefficients[1]) / modA$coefficients[2]
cat('Estimated days to dropout voltage using data after day',dayoffset,':\n')
cat('RevA.3030.high: ',estdays,'\n')

# fit linear model to voltage data
modB = lm(RevB.60.Internal~elapsed, data = df[df$elapsed > dayoffset, ])
# use model coefficients to estimate time to reach lowerlim voltage
estdays = (lowerlim - modB$coefficients[1]) / modB$coefficients[2]
cat('RevB.60.Internal: ',estdays,'\n')

# fit linear model to voltage data
modC = lm(RevB.60.External~elapsed, data = df[df$elapsed > dayoffset, ])
# use model coefficients to estimate time to reach lowerlim voltage
estdays = (lowerlim - modC$coefficients[1]) / modC$coefficients[2]
cat('RevB.60.External: ',estdays,'\n')



################################################################
# Plot voltage time series using the low voltage values on screen
pty = 19
lnwd = 2
ptcex = 1
ylims = c(2.7,4.9)
par(mar = c(5,7,1,1))
ylabs = expression(Voltage*","~3%*%D*"-"*cell)
xlabs = 'Elapsed days'
plot(x = df$elapsed, y = df[,2], 
		ylim = ylims,
		type = 'b',
		las = 1,
		ylab = '',
		xlab = '', 
		cex.axis = 2)
# get current plot boundaries
lims = par()$usr
# Draw grey background
rect(lims[1],lims[3],lims[2],lims[4], col = 'grey90')
box()
# Redraw first line
lines(x=df$elapsed,y=df[,2], type = 'b', lty = 1, col = 'black', lwd = lnwd, 
		pch = pty, cex = ptcex)

# Plot column 3
lines(x=df$elapsed,y=df[,3], type = 'b', lty = 2, col = 'red', lwd = lnwd, 
		pch = pty, cex = ptcex)
# Plot column 4
lines(x=df$elapsed,y=df[,4], type = 'b', lty = 3, col = 'blue', lwd = lnwd,
		pch = pty, cex = ptcex)

# Plot the original battery test data as well
lines(x=df2$elapsed,y=df2[,3], type = 'b', lty = 4, col = 'forestgreen', 
		lwd = lnwd, pch = pty, cex = ptcex)
# Axis labels
mtext(side = 1, text = xlabs, padj = 2.5, cex= 2)
mtext(side = 2, text = ylabs, padj = -2.5, cex = 2)
# Legend
legend('bottomleft',
		legend = c('Rev. A 60:0, internal clock, commit c6b6d38 August, old SD',
				'Rev. A 30:30, internal clock, commit 1a18b1a November, new SD', 
				'Rev. B 60:0, internal clock, commit 1a18b1a November, new SD',
				'Rev. B 60:0, external clock, commit 1a18b1a November, new SD'), 
		lty = c(5,1,2,3,4), lwd = 2,
		col = c('forestgreen','black','red','blue'))
abline(modA, col = 'black')
abline(modB, col = 'red')
abline(modC, col = 'blue')

################################################################################
# Save a 2nd figure copy to the battery_test directory
#battfile = 'D:/Miller_projects/Electronics/OWHL/battery_test/owhl_batt.png'
battfile = 'D:/Miller_projects/Electronics/OWHL/battery_test/owhl_batt.svg'
#png(file = battfile, height = 500, width = 500, units='px')
svg(file = battfile, width = 5.5, height = 5.5, pointsize = 9)
par(mar = c(5,7,1,1))
ylabs = expression(Voltage*","~3%*%~D*"-"*cell)
xlabs = 'Elapsed days'
plot(x = df$elapsed, y = df[,2], 
		ylim = ylims,
		type = 'b',
		las = 1,
		ylab = '',
		xlab = '', 
		cex.axis = 2)
# get current plot boundaries
lims = par()$usr
# Draw grey background
rect(lims[1],lims[3],lims[2],lims[4], col = 'grey90')
box()
# Redraw first line
lines(x=df$elapsed,y=df[,2], type = 'b', lty = 1, col = 'black', lwd = 2, 
		pch = 19)

# Plot column 3
lines(x=df$elapsed,y=df[,3], type = 'b', lty = 2, col = 'red', lwd = 2, 
		pch = 19)
# Plot column 4
lines(x=df$elapsed,y=df[,4], type = 'b', lty = 3, col = 'blue', lwd = 2,
		pch = 19)

# Plot the original battery test data as well
lines(x=df2$elapsed,y=df2[,3], type = 'b', lty = 4, col = 'forestgreen', 
		lwd = 2, pch = 19)
# Axis labels
mtext(side = 1, text = xlabs, padj = 2.5, cex= 2)
mtext(side = 2, text = ylabs, padj = -2.5, cex = 2)
# Date
datetext = paste("Last update: ",Sys.Date())
mtext(side = 1, text = datetext, padj = 2.5, line = 2, adj = -0.25)
# Legend
legend('bottomleft',
		legend = c('Rev. A 60:0, internal clock, commit c6b6d38 August, old SD',
				'Rev. A 30:30, internal clock, commit 1a18b1a November, new SD', 
				'Rev. B 60:0, internal clock, commit 1a18b1a November, new SD',
				'Rev. B 60:0, external clock, commit 1a18b1a November, new SD'), 
		lty = c(5,1,2,3,4), lwd = 2,
		col = c('forestgreen','black','red','blue'))
dev.off()



################################################################################
# The section below creates a windows command line command that runs the 
# ftp_up.bat script I've written to upload the newly generated figure to 
# the lukemiller.org website. 
str1 = "D:/Miller_projects/Electronics/OWHL/battery_test/ftp_up.bat"
#str2 = "D:/Miller_projects/Electronics/OWHL/battery_test/owhl_batt.png"
str2 = "D:/Miller_projects/Electronics/OWHL/battery_test/owhl_batt.svg"
# The command string consists of a call to ftp_up.bat, followed by a space, 
# and then the name of the file that is given to ftp_up.bat as an argument
cmdstr = paste(str1,str2)
# Note that this statement will usually be false, so you'll want to run this
# line of code by hand if you actually want to upload the file. 
if (upload == TRUE)	system(command = cmdstr)


#############################################################################
# This script is used to show the battery usage for a prototype OWHL PCB board
# living in the freezer in lab. The run started on 2014-08-15, with a board
# powered off of 3 D-cell batteries.




############################
# Plot voltage time series
#plot(df$DateTime, df$VoltageLow, type = 'b', 
#		xlab = 'Date + Time',
#		ylab = 'Raw Battery Voltage (3 x D cell)',
#		ylim = c(2.7,5),
#		las = 1)

